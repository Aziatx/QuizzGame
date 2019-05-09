#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <sqlite3.h>

/* portul folosit */
#define PORT 2908

/* lengimea maxima a unui titlu de sectiune */
#define max_titleLength 21

/* lungimea maxima a numelui de utilizator */
#define max_userName 21

/* lungimea maxima a parolei */
#define max_pass 21

/* lungimea sirului random */
#define salt_size 20

/* codul de eroare returnat de anumite apeluri */
extern int errno;

typedef struct thData
{
    int idThread; //id-ul thread-ului tinut in evidenta de acest program
    int cl; //descriptorul intors de accept
    char nume[max_userName]; //numele jucatorului ce utilizeaza threadul
} thData;

typedef struct node
{
    char nume[max_userName]; //numele jucatorilor
    struct node * next;
} node;

typedef struct descriptor
{
    int sock; // descriptorul de socket
    int scor; // scorul jucatorului
    char nume[max_userName]; // numele jucatorului
    struct descriptor * next;
} descriptor;

static void *treat(void *); /* functia executata de fiecare thread ce realizeaza comunicarea cu clientii */
static void *configure_db(void *); /* functia executata cand se doreste modificarea bazei de date in care sunt intrebarile */
static void *broad(void *); /* functia in care se trimit mesajele simultan */
static int recall(void *unused, int argc, char **argv, char **colName); /* functie callback pentru baza de date */

void answer(void *); /* functia de raspuns a serverului catre client */

int autentification(void *); /* functia de autentificare */
void randomstring(char* salt,size_t length); /* functie executata la generarea saltului pentru stocarea parolei */
unsigned long hash(unsigned char *str); /* functie de hash folosita la stocarea parolei */

void print_best(node * first); /* functie ce afiseaza jucatorii conectati la un moment dat */
int add_player(node * first, char new_player[max_titleLength]); /* functie prin care se adauga un nou jucator*/
node * players = NULL; //Inceputul listei ce contine jucatorii logati pe parcursul jocului curent

void add_sock(descriptor * first, int new_sock, char new_name[max_userName]); /* adauga descriptorul de socket pentru "broadcast" */
void print_sock(descriptor * first); /* afiseaza socketul, numele si scorul */
void delete_sock(struct descriptor **head_ref, int key); /* functie de stergere a socketului din "broadcast" in caz de deconectare*/
struct descriptor *copy(struct descriptor *start1); /* functie de copiere a listei de "broadcast" */
descriptor * broadcast = NULL;//Inceputul liste pentru "broadcast"

int inGame = 1;//Variabila ce monitorizeaza starea serverului: 1- in joc; 0-asteptare pentru noul joc-> se pot adauga intrebari in BD

int main ()
{
    srand(time(NULL)); // seed folosit la generarea unui string random
    struct sockaddr_in server;	//structura folosita de server
    struct sockaddr_in from;
    int nr;		//mesajul primit de trimis la client
    int sd;		//descriptorul de socket
    int pid;
    pthread_t th[100];    //Identificatorii thread-urilor care se vor crea
    int i=2; //Threadul cu id = 1 va fi rezervat threadului ce se ocupa de trimiterea intrebarilor catre clienti
	     //Threadul cu id = 2 va fi rezervat threadului ce se ocupa cu incarcarea intrebarilor in BD
   
    const char *message = "Configurarea bazei de date";
    sqlite3 *quiz;
    
    players = malloc(sizeof(node));

    strcpy(players->nume, "NUMELE UTILIZATORULUI");
    players->next = NULL;

    /* crearea threadului ce se va ocupa cu configurarea bazei de date */
    if (pthread_create(&th[2], NULL, &configure_db, (void*) message))
    {
        perror ("[server]Eroare la crearea threadului de configurare.\n");
        return errno;
    }

    /* crearea threadului ce se va ocupa cu broadcast */
    if (pthread_create(&th[1], NULL, &broad, (void*) message))
    {
        perror ("[server]Eroare la crearea threadului de configurare.\n");
        return errno;
    }

    /* crearea unui socket */
    if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror ("[server]Eroare la socket().\n");
        return errno;
    }
    /* utilizarea optiunii SO_REUSEADDR */
    int on=1;
    setsockopt(sd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));

    /* pregatirea structurilor de date */
    bzero (&server, sizeof (server));
    bzero (&from, sizeof (from));

    /* umplem structura folosita de server */
    /* stabilirea familiei de socket-uri */
    server.sin_family = AF_INET;
    /* acceptam orice adresa */
    server.sin_addr.s_addr = htonl (INADDR_ANY);
    /* utilizam un port utilizator */
    server.sin_port = htons (PORT);

    /* atasam socketul */
    if (bind (sd, (struct sockaddr *) &server, sizeof (struct sockaddr)) == -1)
    {
        perror ("[server]Eroare la bind().\n");
        return errno;
    }

    /* punem serverul sa asculte daca vin clienti sa se conecteze */
    if (listen (sd, 10) == -1)
    {
        perror ("[server]Eroare la listen().\n");
        return errno;
    }
   
    while (1)
    {
        int client;
        thData * td; //parametru functia executata de thread
        int length = sizeof (from);

        printf ("[server]Asteptam la portul %d...\n",PORT);
        fflush (stdout);

        // client= malloc(sizeof(int));
        /* acceptam un client (stare blocanta pina la realizarea conexiunii) */
        if ( (client = accept (sd, (struct sockaddr *) &from, &length)) < 0)
        {
            perror ("[server]Eroare la accept().\n");
            continue;
        }


        /* s-a realizat conexiunea, se astepta mesajul */

        // int idThread; //id-ul threadului
        // int cl; //descriptorul intors de accept

        td=(struct thData*)malloc(sizeof(struct thData));
        td->idThread=i++;
        td->cl=client;
	
        pthread_create(&th[i], NULL, &treat, td);
    }//while
}

void print_best(node * first)
{
    node * actual = first;

    while (actual != NULL)
    {
        printf("%s \n", actual->nume);
        actual = actual->next;
    }
};

void print_sock(descriptor * first)
{
    descriptor * actual = first;

    while (actual != NULL)
    {
        printf("%d| %s |%d \n", actual->sock, actual->nume, actual->scor);
        actual = actual->next;
    }
};


int add_player(node * first, char new_player[max_userName])
{
    node * actual = first;
    int ok = 1;
    if (strcmp(actual->nume, new_player) == 0)
    {
        printf("[server]Jucatorul a parasit deja jocul/ se afla in joc: %s.\n", new_player);
        ok = 0;
    }

    while (actual->next != NULL)
    {
        actual = actual->next;

        if (strcmp(actual->nume, new_player) == 0)
        {
            printf("[server]Jucatorul a parasit deja jocul/ se afla in joc: %s.\n", new_player);
            ok = 0;
        }
    }
    if (ok == 1)
    {
        actual->next = malloc(sizeof(node));
        strcpy(actual->next->nume, new_player);
        actual->next->next = NULL;
    }
    return ok;
};

struct descriptor *copy(struct descriptor *start1)
{
    struct descriptor *start2=NULL,*previous=NULL;

    while(start1!=NULL)
    {
        struct descriptor * temp = (struct descriptor *) malloc (sizeof(struct descriptor));
        temp->sock=start1->sock;
        temp->scor=start1->scor;
        strcpy(temp->nume,start1->nume);
        temp->next=NULL;

        if(start2==NULL)
        {
            start2=temp;
            previous=temp;
        }
        else
        {
            previous->next=temp;
            previous=temp;
        }
        start1=start1->next;
    }
    return start2;
};

void add_sock(descriptor * first, int new_sock, char new_name[max_userName])
{
    descriptor * actual = first;

    if (actual->sock == new_sock)
    {
        printf("[server]Deja tinem minte acest descriptor de socket: %d.\n", new_sock);
        return ;
    }

    while (actual->next != NULL)
    {
        actual = actual->next;

        if (actual->sock == new_sock)
        {
            printf("[server]Deja tinem minte acest descriptor de socket: %d.\n", new_sock);
            return ;
        }
    }

    actual->next = malloc(sizeof(descriptor));
    actual->next->sock = new_sock;
    strcpy(actual->next->nume, new_name);
    actual->next->scor = 0;
    actual->next->next = NULL;
};

void delete_sock(descriptor **head_ref, int key)
{
    
    struct descriptor* temp = *head_ref, *previous;

    if (temp != NULL && temp->sock == key)
    {
        *head_ref = temp->next;   
        free(temp);              
        return;
    }

    while (temp != NULL && temp->sock != key)
    {
        previous = temp;
        temp = temp->next;
    }

    if (temp == NULL)
	return;

    previous->next = temp->next;

    free(temp);
};

static void *treat(void * arg)
{
    struct thData tdL;
    tdL= *((struct thData*)arg);
    printf ("[thread]- %d - Asteptam mesajul...\n", tdL.cl);
    fflush (stdout);
    pthread_detach(pthread_self());
    /* autentificarea clientului */
    answer((struct thData*)arg);

    /* am terminat cu acest client, inchidem conexiunea */
    close ((intptr_t)arg);
    return(NULL);

};

static int recall(void *unused, int argc, char **argv, char **colName)
{
    for(int i = 0; i < argc; i++)
    {
        printf("%s = %s\n", colName[i], argv[i] ? argv[i] : "NULL");
    }
    return 0;
};

static void *broad(void * arg)
{
    while(1)
    {
        printf("Am inceput un nou joc!");
        sqlite3 *quiz;
	
	/* adaugam in lista de broadcast pe jucatorii care au ales sa ramana conectati pentru un nou joc */
        players = NULL;
        players = malloc(sizeof(node));
	strcpy(players->nume, "NUMELE UTILIZATORULUI");
        players->next = NULL;

        descriptor* lookafter = broadcast;
        if(lookafter != NULL)
            add_player(players, lookafter->nume);
        while (lookafter!=NULL)
        {
            add_player(players, lookafter->nume);
            lookafter = lookafter->next;
        }

        inGame = 1; // intram in joc -> nu mai pot fi modificate intrebarile
        char *error = 0;

	/* conectarea la BD */
        int conn = sqlite3_open_v2("quizzgame.db", &quiz,SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |SQLITE_OPEN_NOMUTEX, "unix");
        
        if (conn)
        {
            fprintf(stderr, "[server]Nu se poate deschide baza de date: %s\n", sqlite3_errmsg(quiz));
            return(0);
        }
        else
        {
            fprintf(stdout, "[server]S-a stabilit comunicarea cu baza de date.\n");
        }
        
        /* construirea interogarii care ne va permite sa trimitem treptat intrebarile */
        char select[70]="SELECT ID, BODY, CHOICE1, CHOICE2, CHOICE3, CHOICE4 FROM QUESTIONS;";
        sqlite3_stmt *statement;
        conn = sqlite3_prepare_v2(quiz, select, strlen(select)+1, &statement, NULL);

        if( conn != SQLITE_OK)
        {
            fprintf(stderr, "Nu s-au putut incarca intrebarile. Eroare: %s.\n", error);
            sqlite3_free(error);
        }
        else
        {
            fprintf(stdout, "Intrebarile au fost incarcate.\n");
        }
        
        /* variabilele in care se vor introduce intrebarea si variantele de raspuns */
        int id;
        char body[256];
        char choice1[256];
        char choice2[256];
        char choice3[256];
        char choice4[256];
	
        char message[256*5+13]; //mesajul ce va fi transmis

        int opt; //optiunea citita de la client
	
	/* incepem iterarea prin lista de "broadcast" */
        descriptor* actual;
        actual = copy(broadcast);
       
        
        do //cat timp exista intrebari
        {
	   
            bzero(&id,sizeof(id));
            bzero(&body,sizeof(body));
            bzero(&choice1,sizeof(choice1));
            bzero(&choice2,sizeof(choice2));
            bzero(&choice3,sizeof(choice3));
            bzero(&choice4,sizeof(choice4));
            bzero(&message, sizeof(message));
            bzero(&opt, sizeof(opt));

            conn = sqlite3_step (statement) ;
            if (conn == SQLITE_ROW)
            {
                id = sqlite3_column_int(statement,0) ;
                strcpy(body, (char *)sqlite3_column_text(statement,1)) ;
                strcpy(choice1, (char *)sqlite3_column_text(statement,2)) ;
                strcpy(choice2, (char *)sqlite3_column_text(statement,3)) ;
                strcpy(choice3, (char *)sqlite3_column_text(statement,4)) ;
                strcpy(choice4, (char *)sqlite3_column_text(statement,5)) ;
		//printam intrebarea si in server, pentru verificare; poate fi eliminat
                printf("%s \n %s \t %s \n %s \t %s \n", body, choice1, choice2, choice3, choice4);
            }
            else printf("Dintr-un anumit motiv nu putem citi date\n");
		
	    /* construim mesajul */
            strcat(message, body);
            strcat(message, "\n1.");
            strcat(message, choice1);
            strcat(message, "\t2.");
            strcat(message, choice2);
            strcat(message, "\n3.");
            strcat(message, choice3);
            strcat(message, "\t4.");
            strcat(message, choice4);
            strcat(message, "\n");

	    /* daca mesajul este gol, inseamna ca ne aflam la ultima intrebare; trimitem castigatorul jocului */
            if (strlen(body)<=0)
            {
                descriptor* lookfor = broadcast;
                int max = 0;
                char numele[max_userName];
                char scor[3];

                while(lookfor!=NULL)
                {
                    if (lookfor->scor > max)
                    {
                        strcpy(numele,lookfor->nume);
                        max = lookfor->scor;
                    }
                    lookfor = lookfor -> next;
                }

                bzero(&message, sizeof(message));
                strcat(message, "Cel mai bun jucator este: ");
                strcat(message,numele);
                strcat(message," cu scorul ");
                sprintf(scor,"%d",max);
                strcat(message,scor);
                strcat(message,".\n");
            }

            // afisam in server ca intrebarea nu are cui sa fie transmisa; poate fi eliminat
            if(actual == NULL) printf("Nu exista clienti conectati.\n");

	    // retienm lista "broadcast" inca o data pentru reiterarea necesara citirii variantelor de raspuns 
            descriptor* actual2 = actual;

            while(actual != NULL)
            {

                if (actual->sock > 0 && actual->sock < 1000)
                {
                    if (write (actual->sock, &message, sizeof(message)) <= 0)
                    {
                        printf("[Thread %d] ",actual->sock);
                        delete_sock(&broadcast, actual->sock);
                        print_sock(broadcast);
                        perror ("Eroare la transmiterea intrebarii catre client.\n");
                        continue;
                    }
                }
                actual = actual -> next;
            }

            printf("Intrebarea a fost trimisa. Se asteapta raspunsuri...\n");
            
            sleep(10); // cat timp asteptam raspunsul de la client

            while (actual2 != NULL)
            {
                if(actual2->sock > 0 && actual2->sock < 1000)
                {
                    if (read (actual2->sock, &opt,sizeof(opt)) <= 0)
                    {
                        printf("[Thread %d]\n", actual2->sock);
                        delete_sock(&broadcast, actual2->sock);
                        print_sock(broadcast);
                        perror ("Eroare la citirea variantei corecte2.\n");
                        continue;
                    }
                   
                    if (id%10 == opt) // daca este varianta corecta !CONVETIE id
                    {
			//marim scorul
                        actual2->scor++;
                        descriptor* lookfor = broadcast ;
                        while(strcmp(lookfor->nume, actual2->nume)!= 0)
                            lookfor = lookfor->next;
                        lookfor->scor++;

                    }
		    /* daca s-a cerut deconectarea, deconectam */
                    if (opt == 5)
                    {
                        delete_sock(&broadcast, actual2->sock);
                        close((intptr_t)actual2->scor);
                    }
                }
                actual2 = actual2 -> next;
            }

            print_sock(broadcast); // afisam scorurile in server
            actual = copy(broadcast); // copiem lista de "broadcast" -> posibile moficari prin deconectarea unor clienti si conectarea altora
            printf("Se pregateste trimitarea urmatoarei intrebari\n");
            sleep(4);
        }
        while (conn == SQLITE_ROW);

        sqlite3_finalize(statement);//Am trimis toate intrebarile
        sqlite3_close(quiz);
        printf("Asteptam inceperea jocului urmator.\n");
	/* resetam scorurile pentru cei care au ramas in joc */
        descriptor *lookfor = broadcast;
        while(lookfor!=NULL)
        {
            lookfor->scor = 0;
            lookfor = lookfor -> next;
        }
        inGame = 0; // permitem modificarea BD cu intrebari
        sleep(30); // Asteptam 30 de secunde inaintea inceperii jocului urmator
    }
};

static void *configure_db(void * arg)
{
    printf("Pentru a adauga o intrebare in baza de date, introduceti comanda !add in perioada de asteptare dintre jocuri. Formatul intrebarii este urmatorul: id (cu urmatoarele conventii: prima cifra reprezinta numarul sectiunii, urmatoarele 3 a cata intrebare este din sectiunea respectiva si ultima varianta corecta 1/2/3/4; corpul intrebarii si cele 4 variante cu cel mult 255 de caractere).\n");
    char string[4];
    while (1)
    {
        scanf("%s",string); //apel blocant
        if (strcmp(string,"!add") == 0 && strlen(string) == 4 && inGame == 0)
        {
            char id[5];
            char body[256];
            char choice1[256];
            char choice2[256];
            char choice3[256];
            char choice4[256];

            /* citirea intrebarii si a variantelor de raspuns */
            printf("Introduceti id-ul intrebarii:");
            scanf("%s",id);

            printf("Introduceti textul intrebarii:");
            scanf("%s",body);

            printf("Introduceti prima varianta de raspuns:");
            scanf("%s",choice1);

            printf("Introduceti a doua varianta de raspuns:");
            scanf("%s",choice2);

            printf("Introduceti a treia varianta de raspuns:");
            scanf("%s",choice3);

            printf("Introduceti a patra varianta de raspuns:");
            scanf("%s",choice4);

            /* construirea comenzii de insertie */
            char construct[1300];
            strcpy(construct,"INSERT INTO QUESTIONS (ID,BODY,CHOICE1,CHOICE2,CHOICE3,CHOICE4)  VALUES (");
            strcat(construct, id);
            strcat(construct, ", '");
            strcat(construct, body);
            strcat(construct, "', '");
            strcat(construct, choice1);
            strcat(construct, "','");
            strcat(construct, choice2);
            strcat(construct, "','");
            strcat(construct, choice3);
            strcat(construct, "','");
            strcat(construct, choice4);
            strcat(construct, "');");

            /*stabilirea conexiunii cu baza de date */
            sqlite3 *quiz;
            char *error = 0;
            int conn = sqlite3_open_v2("quizzgame.db", &quiz,SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |SQLITE_OPEN_NOMUTEX, "unix");

            if (conn)
            {
                fprintf(stderr, "[server]Nu se poate deschide baza de date: %s\n", sqlite3_errmsg(quiz));
                return(0);
            }
            else
            {
                fprintf(stdout, "[server]S-a stabilit comunicarea cu baza de date.\n");
            }

            char * insert = construct;

            conn = sqlite3_exec(quiz, insert, recall, 0, &error);

            if( conn != SQLITE_OK)
            {
                fprintf(stderr, "Nu s-a putut introduce intrebarea. Eroare: %s.\n", error);
                sqlite3_free(error);
            }
            else
            {
                fprintf(stdout, "Intrebarea a fost adaugata.\n");
            }
	
            sqlite3_close(quiz);
        }
        else if (inGame == 1)
        {
            printf("Nu se pot actualiza intrebarile in timpul jocului.\n");
        }
        bzero( &string, sizeof(string));
    }
    return(NULL);
};

void randomstring(char* salt,size_t length)
{
    const char characters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,.-#?!";

    int size = (int) (sizeof(characters) -1);
    int key;
	
    for (int n = 0; n < length; n++)
    {
        {
            key = rand() % size;
            salt[n] = characters[key];
        }

        salt[length] = '\0';

    }
};

unsigned long hash(unsigned char *str)
{
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c;

    return hash;
};

int autentification(void *arg)
{
    struct thData tdL;
    tdL= *((struct thData*)arg);
    int nr;
    int logat = 0;
    char first_message[100] = "\t QUIZZGAME \nAlegeti optiunea: \n1.Conectare\n2.Inregistrare\n3.Iesire\n";//mesajul de bunvenit

    if (write (tdL.cl, &first_message, sizeof(first_message)) <= 0)
    {
        printf("[Thread %d] ",tdL.idThread);
        perror ("[Thread]Eroare la transmiterea mesajului de bunvenit catre client.\n");


    }
    else
        printf ("[Thread %d]Mesajul de bunvenit a fost trasmis cu succes.\n",tdL.idThread);

    if (read (tdL.cl, &nr,sizeof(int)) <= 0)
    {
        printf("[Thread %d]\n",tdL.idThread);
        perror ("Eroare la citirea optiunii de autentificare.\n");

        delete_sock(&broadcast, tdL.cl);
	//In caz de deconectare fortata (CTRL + C)
        int v = 1;
        close(tdL.cl);
        pthread_exit(&v);
    }

    printf ("[Thread %d]Mesajul a fost receptionat...%d\n",tdL.idThread, nr);

    if (nr == 1) //conectare
    {
        char userName[max_userName]; 
        char salt[salt_size];
        char hashedPass[50];
        char password[max_pass];
        char passplussalt[max_pass+salt_size]; // pentru functia hash
        char pack[max_userName + max_pass+ 2]; // userName + parola
        char current_hashedPass[50]; // valoarea hash a parolei curente
        int ok = 0;
        while ( ok == 0) // cat timp nu ne-am putut loga
        {
            bzero(&userName, sizeof(userName));
            bzero(&password, sizeof(password));
            bzero(&pack, sizeof(pack));
            bzero(&current_hashedPass, sizeof(current_hashedPass));
            bzero(&passplussalt, sizeof(passplussalt));
     
            if (read (tdL.cl, &pack, sizeof(pack)) <= 0)
            {
                printf("[Thread %d]\n",tdL.idThread);
                perror ("Eroare la citirea numelui de utilizator.\n");
                delete_sock(&broadcast, tdL.cl);
                int v = 1;
                close(tdL.cl);
                pthread_exit(&v);
            }

            //printf ("[Thread %d]Pack a fost receptionat: %s.\n",tdL.idThread, pack);

            strcpy(userName, strtok(pack, "`"));
            strcpy(password, strtok(NULL, "`"));
            
            sqlite3 *quiz;
            char *error = 0;
            int conn = sqlite3_open_v2("user.db", &quiz,SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |SQLITE_OPEN_NOMUTEX, "unix");

            if (conn)
            {
                fprintf(stderr, "[server]Nu se poate deschide baza de date: %s\n", sqlite3_errmsg(quiz));
                return(0);
            }
            else
            {
                fprintf(stdout, "[server]S-a stabilit comunicarea cu baza de date.\n");
            }
	    /* cautam numele transmis. daca exista, tinem minte saltul si parola hash */
            sqlite3_stmt *statement;
            char select[80] = "SELECT salt, hashedPass from USERS where userName ='";
            strcat(select,userName);
            strcat(select,"';");
            conn = sqlite3_prepare_v2(quiz, select, strlen(select)+1, &statement, NULL);

            if( conn != SQLITE_OK)
            {
                fprintf(stderr, "Nu s-a putut realiza interogarea. Eroare: %s.\n", error);
                sqlite3_free(error);
            }
            else
            {
                fprintf(stdout, "Interogarea a fost realizata.\n");
            }
            
            conn = sqlite3_step (statement);
            int userFound = 0;

            if (conn == SQLITE_ROW)
            {
                strcpy(salt, (char *)sqlite3_column_text(statement,0));
                strcpy(hashedPass, (char *)sqlite3_column_text(statement,1));
                userFound = 1;
            }
            
            sqlite3_finalize(statement);
            sqlite3_close(quiz);

            if (userFound == 1)
            {
                strcat(passplussalt, password);
                strcat(passplussalt, salt);
                sprintf(current_hashedPass,"%lu",hash(passplussalt));

                if (strcmp(current_hashedPass, hashedPass) == 0)
                {
                    int not = add_player(players,userName);
                    if (not == 1)
                    {
                        ok = 1;
                        logat = 1;
                        strcpy(tdL.nume, userName);
                    }
                }

                bzero(&passplussalt, sizeof(passplussalt));
            }

            if (write (tdL.cl, &ok, sizeof(int)) <= 0)
            {
                printf("[Thread %d] ",tdL.idThread);
                perror ("[Thread]Eroare la transmiterea OK-ului petrnu nume.\n");
            }
            else
                printf ("[Thread %d]Mesajul de bunatate a ok-ului a fost transmis\n",tdL.idThread);

            printf("Liviu Dragnea\n");
        }
    }

    else if (nr == 2) // am inceput autentificare
    {
        printf("Am inceput autentificarea.\n");
        /* verificam daca nu exista deja un utilizator cu acelasi nume in BD */

        char userName[max_userName];
        int ok = 0;
        sqlite3 *quiz;
        char *error = 0;

        int conn = sqlite3_open_v2("user.db", &quiz,SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |SQLITE_OPEN_NOMUTEX, "unix");

        if (conn)
        {
            fprintf(stderr, "[server]Nu se poate deschide baza de date: %s\n", sqlite3_errmsg(quiz));
            return(0);
        }
        else
        {
            fprintf(stdout, "[server]S-a stabilit comunicarea cu baza de date.\n");
        }
        while (ok == 0)
        {
            /* citim numele dorit */
            bzero(&userName, sizeof(userName));
            char select[62] = "SELECT * FROM USERS WHERE userName = '";
            if (read (tdL.cl, &userName,sizeof(userName)) <= 0)
            {
                printf("[Thread %d]\n",tdL.idThread);
                perror ("Eroare la citirea numelui de utilizator.\n");
                delete_sock(&broadcast, tdL.cl);
                int v = 1;
                close(tdL.cl);
                pthread_exit(&v);
            }
            
            /* verificam daca acesta nu se afla deja in baza de date */

            /* construim interogarea */

            strcat(select, userName);
            strcat(select, "';");
           
            sqlite3_stmt *statement;
            conn = sqlite3_prepare_v2(quiz, select, strlen(select)+1, &statement, NULL);

            if( conn != SQLITE_OK)
            {
                fprintf(stderr, "Nu s-a putut cauta numele. Eroare: %s.\n", error);
                sqlite3_free(error);
            }
           
            conn = sqlite3_step (statement);
            if (conn != SQLITE_ROW)
            {
                printf("[server]S-a incercat adaugarea unui cont cu nume deja existent.\n");
                ok = 1;
            }
           
            sqlite3_finalize(statement);
            sqlite3_close(quiz);

            if (write (tdL.cl, &ok, sizeof(int)) <= 0)
            {
                printf("[Thread %d] ",tdL.idThread);
                perror ("[Thread]Eroare la transmiterea OK-ului petrnu nume.\n");
            }
            else
                //printf ("[Thread %d]Mesajul de bunatate a ok-ului a fost transmis.\n",tdL.idThread);
            bzero(&select, sizeof(select));
        }

	/* dupa ce a fost transmis un nume valid, citim parola dorita */

        char password[max_pass];

        if (read (tdL.cl, &password,sizeof(password)) <= 0)
        {
            printf("[Thread %d]\n",tdL.idThread);
            perror ("Eroare la citirea parolei.\n");
            delete_sock(&broadcast, tdL.cl);
            int v = 1;
            close(tdL.cl);
            pthread_exit(&v);
        }

        
        char salt[salt_size];
        randomstring(salt, salt_size -1);
        
        char passplussalt[max_pass + salt_size];
        strcpy(passplussalt,password);
        strcat(passplussalt,salt);
        
        unsigned long hashed = hash(passplussalt);
       
        char hashedString[50];
        sprintf(hashedString,"%lu",hashed);
        /* introducem in baza de date jucatorul */

        char construct[150];
        strcpy(construct,"INSERT INTO USERS (userName, salt, hashedPass) VALUES ('");
        strcat(construct,userName);
        strcat(construct,"','");
        strcat(construct, salt);
        strcat(construct,"','");
        strcat(construct, hashedString);
        strcat(construct, "');");
       
        bzero(&salt, sizeof(salt));
        bzero(&password, sizeof(password));
        bzero(&passplussalt, sizeof(passplussalt));
        bzero(&hashedString, sizeof(hashedString));
      
        conn = sqlite3_open_v2("user.db", &quiz,SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |SQLITE_OPEN_NOMUTEX, "unix");

        if (conn)
        {
            fprintf(stderr, "[server]Nu se poate deschide baza de date: %s\n", sqlite3_errmsg(quiz));
            return(0);
        }
        else
        {
            fprintf(stdout, "[server]S-a restabilit comunicarea cu baza de date.\n");
        }
        char * insert = construct;

        conn = sqlite3_exec(quiz, insert, recall, 0, &error);

        if( conn != SQLITE_OK)
        {
            fprintf(stderr, "Nu s-a putut introduce utilizatorul. Eroare: %s.\n", error);
            sqlite3_free(error);
        }
        else
        {
            fprintf(stdout, "Utilizator adaugat.\n");
            logat = 1;
            add_player(players,userName);
            strcpy(tdL.nume, userName);
        }
        sqlite3_close(quiz);
    }
    else if (nr == 3)
    {
        printf("Jucatorul a iesit.\n");
    }
   
    return logat;
};

void answer(void *arg)
{
    int nr, i=0;
    int login = 0;
    struct thData tdL;
    tdL= *((struct thData*)arg);
    login = autentification((struct thData*)arg);

    // numele jucatorului este ultimul nume din players adaugat in lista
    if (login == 1)
    {
        node * actual = players;

        while (actual->next != NULL)
        {
            actual = actual->next;
        }
        strcpy(tdL.nume, actual->nume);
        
        //printf("[Thread %d]NUMELE THREADULUI...%s\n",tdL.idThread, tdL.nume);
        strcpy(tdL.nume, actual->nume);
        print_best(players);
        
	/* daca un jucator s-a autentificat, il adaugam la lista de "broadcast" */
	if (broadcast == NULL)
        {
            broadcast = malloc(sizeof(descriptor));
            broadcast->sock = tdL.cl;
            printf("NUmeLE este: %s", tdL.nume);
            strcpy(broadcast->nume, tdL.nume);
            broadcast->scor = 0;
            broadcast->next = NULL;
        }
        else add_sock(broadcast, tdL.cl, tdL.nume);
    }
}
