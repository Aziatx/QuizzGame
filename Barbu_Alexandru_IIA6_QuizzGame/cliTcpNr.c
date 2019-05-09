#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <pwd.h>
#include <curses.h>
#include <arpa/inet.h>
#include <poll.h>

/* lungimea maxima a numelui de utilizator */
#define max_userName 21

/* lungimea maxima a parolei */
#define max_pass 21

/* codul de eroare returnat de anumite apeluri */
extern int errno;

/* portul de conectare la server*/
int port;

int main (int argc, char *argv[])
{
    int sd;			// descriptorul de socket
    struct sockaddr_in server;	// structura folosita pentru conectare
    // mesajul trimis
    int nr=0;
    char log[100], opt[2];

    /* exista toate argumentele in linia de comanda? */
    if (argc != 3)
    {
        printf ("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
        return -1;
    }

    /* stabilim portul */
    port = atoi (argv[2]);

    /* cream socketul */
    if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror ("Eroare la socket().\n");
        return errno;
    }

    /* umplem structura folosita pentru realizarea conexiunii cu serverul */
    /* familia socket-ului */
    server.sin_family = AF_INET;
    /* adresa IP a serverului */
    server.sin_addr.s_addr = inet_addr(argv[1]);
    /* portul de conectare */
    server.sin_port = htons (port);

    /* ne conectam la server */
    if (connect (sd, (struct sockaddr *) &server,sizeof (struct sockaddr)) == -1)
    {
        perror ("[client]Eroare la connect().\n");
        return errno;
    }

    /* citirea mesajului de bunvenit + optiunile de autentificare */
    if (read (sd, &log,sizeof(log)) < 0)
    {
        perror ("[client]Eroare la read() de la server.\n");
        return errno;
    }
    printf("%s\n", log);
    fflush (stdout);

    /* alegerea optiunii de autentificare */
    while ( nr != 1 && nr != 2 && nr != 3)
    {
        printf ("Introduceti numarul optiunii alese: ");
        fflush (stdout);
        read (0, opt, sizeof(opt));
        nr = atoi(opt);
        printf("[client] Am citit optiunea %d\n",nr);
        if ( nr != 1 && nr != 2 && nr!=3 )
            printf("Numarul introdus nu are asociata o optiune. Reincercati...\n");
    }

    /* trimiterea optiunii la server */
    if (write (sd,&nr,sizeof(int)) <= 0)
    {
        perror ("[client]Eroare la write() spre server.\n");
        return errno;
    }

    char userName[max_userName];
    /* tratarea loginului */
    /* transmiterea numelui de utilizator */
    if(nr == 1)
    {
        char password[max_pass];
        char pack[max_userName + max_pass+ 2];
        int ok = 0;
        while (ok == 0)
        {
            bzero(&pack, sizeof(pack));

            printf ("Introduceti numele de utilizator: ");
            scanf ("%s", userName);
            strcat(pack, userName);

            strcat(pack, "`");

            strncpy(password, getpass("Introduceti parola :"), max_pass);
            printf("[client]S-a introdus parola : %s .\n", password);
            strcat(pack,password);

            if (write (sd, &pack, sizeof(pack)) <= 0)
            {
                perror ("[client]Eroare la trimiterea numelui spre server.\n");
                return errno;
            }

            if (read (sd, &ok,sizeof(int)) < 0)
            {
                perror ("[client]Eroare la read() de la server.\n");
                return errno;
            }
            printf("Combinatie valida:%d\n", ok);
            fflush (stdout);

            if (ok == 1)
                printf("Logare cu succes!\n");
            else
		printf("Jucator inexistent/ Eliminat din runda actuala.\n");
        }
    }
    else if (nr == 2)
    {
        int ok = 0;
        /* trimitem nume de utilizator pana gasim unul inexistent in baza de date */
        while (ok == 0)
        {
            printf ("Introduceti numele de utilizator pe ca il doriti (folositi maximum %d caractere): ",max_userName);
            scanf ("%s", userName);
            printf("[client]Am citit numele: %s\n", userName);

            if (write (sd, &userName, sizeof(userName)) <= 0)
            {
                perror ("[client]Eroare la write() spre server.\n");
                return errno;
            }
            if (read (sd, &ok,sizeof(int)) < 0)
            {
                perror ("[client]Eroare la read() de la server.\n");
                return errno;
            }
            printf("OK ESTE:%d\n", ok);
            fflush (stdout);
        }
        /* citim parola dorita */
        char password[max_pass];
        printf ("[client]Introduceri parola dorita (folositi maximum %d de caractere): ", max_pass);
        strncpy(password, getpass("Introduceti parola :"), max_pass);
        printf("[client]S-a introdus parola : %s .", password);

        /* trimitem parola */
        if (write (sd, &password, sizeof(password)) <= 0)
        {
            perror ("[client]Eroare la transmiterea parolei catre server.\n");
            return errno;
        }
    }
    else if (nr == 3)
    {
	/* inchidem socketul */
        close (sd);
        return 0;
    }
    printf("INCEPEREA JOCULUI\n");
    char message[256*5+13];
    int ans = 0; // raspunsul ales la o intrebare

    while(1)
    {
        if (read (sd, &message,sizeof(message)) < 0)
        {
            perror ("[client]Eroare la broad read.\n");
            return errno;
        }
        printf("________________________________________\n%s\n",message);
	if(strstr(message,"Cel mai bun jucator este: ")!=NULL)
	   printf("Daca doriti deconectarea, introduceti 5. Altfel introduceti 1.\n");
        else printf("Daca doriti sa iesiti din joc, introduceti 5 ca varianta corecta.Introduceti raspunsul corect:\n");
        fflush (stdout);
        char ch[2] = "0";
        
	/* asteptam 9 secunde pentru citirea raspunsului */
        struct pollfd mypoll = { STDIN_FILENO, POLLIN|POLLPRI };
        if( poll(&mypoll, 1, 9000) )
        {
            scanf("%s", ch);
            printf("Varianta aleasa: %s\n", ch);
        }
        else
        {
            puts("Nu ati transmis nicio varianta.\n");
        }

	/* verificarea pentru asigurarea validitatii optiunii */
        if (strcmp(ch,"0")!=0 && strcmp(ch,"1")!=0 && strcmp(ch,"2") && strcmp(ch,"3")!=0 && strcmp(ch,"4")!=0 && strcmp(ch,"5")!=0)
            strcpy(ch,"0");

        ans = atoi(ch);
        if (write (sd, &ans, sizeof(ans)) <= 0)
        {
            perror ("[client]Eroare la broad write.\n");
            return errno;
        }
        if(ans == 5) // daca s-a dorit deconectarea, inchidem socketul
        {
            sleep(6);
            close(sd);
            return 5;
        }
    }
}
