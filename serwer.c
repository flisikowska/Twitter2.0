#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>

#define MY_MSG_SIZE 100 //Maksymalny rozmiar wiadomosci
#define LOGIN_SIZE 20 //Maksymalny rozmiar loginu uzytkownika

key_t shm_entries_key;
key_t shm_state_key;
key_t sem_entries_key;
key_t sem_counter_key;
int shm_entries_id;
int shm_state_id;
int sem_entries_id;
int sem_counter_id;

//Struktura dla wpisu
struct entry{
	char username[LOGIN_SIZE];
	char message[MY_MSG_SIZE];
	int likes;
};

//Struktura dla stanu serwera
struct state{
	int n; //Maksymalna liczba wpisow
	int counter; //Aktualna liczba wpisow
};

//Wskazniki do pamieci wspoldzielonej
struct entry * shm_entries;
struct state * shm_state;
int * semaphores;

//Funkcja do obslugi sygnalu SIGSTP 
void handle_sigtstp(int sigint){
	printf("\n");
	if(shm_state->counter == 0)
		printf("Brak wpisow\n");
	else{
		printf("__________  Twitter 2.0:  __________\n");
		for(int i=0; i<shm_state->counter; i++){
			if(strlen(shm_entries[i].message) > 0)
				printf("[%s]: %s [Polubienia: %d]\n",shm_entries[i].username, shm_entries[i].message, shm_entries[i].likes);
		}
	}
}

//Funkcja do obslugi sygnalu SIGINT
void handle_sigint(int sigint){
	printf("[Serwer]: dostalem SIGINT => koncze i sprzatam...\n");

	//Usuniecie listy semaforow
	for(int i=0; i< shm_state->n; i++)
		semctl(sem_entries_id, i, IPC_RMID);

	//Usuniecie semafora countera
	semctl(sem_counter_id, 0, IPC_RMID);

	//Odlaczenie i usuniecie pamieci wspoldzielonych
	printf("            (odlaczenie (wpisy)  %s, odlaczenie (stan): %s, usuniecie (wpisy) %s, usuniecie (stan): %s)\n", 
					(shmdt(shm_entries) == 0) ? "OK" : "blad shmdt", 
					(shmdt(shm_state) == 0) ? "OK" : "blad shmdt", 
					(shmctl(shm_entries_id, IPC_RMID, 0) == 0) ? "OK" : "blad shmctl",
					(shmctl(shm_state_id, IPC_RMID, 0) == 0) ? "OK" : "blad shmctl");
	free(semaphores);
    	printf("[Serwer]: Zasoby zwolnione, program zakończony.\n");
	exit(0);
}

int main(int argc, char* argv[]){
	struct shmid_ds buf;
	printf("[Serwer]: Twitter 2.0 (wersja C)\n");
	
	//Rejestracja obslugi sygnalow
	signal(SIGINT, handle_sigint);
	signal(SIGTSTP, handle_sigtstp);

	if(argc !=3){
		fprintf(stderr, "Blad: Niewlasciwa liczba argumentow.\n");
		exit(1);
	}

	//Pobranie liczby wpisow z argumentow programu
	int n=atoi(argv[2]);
	if(n<=0){
		fprintf(stderr, "Blad: Maksymalna ilosc wpisow nie moze byc mniejsza niz 1\n");
		exit(1);
	}
	
	//Tworzenie klucza dla pamieci wspoldzielonej (wpisy)
	printf("[Serwer]: tworze klucz dla wpisow na podstawie pliku ./%s...\n", argv[1]);
	if((shm_entries_key= ftok(argv[1], 'R')) == -1){
		perror("ftok()");
		exit(1);
	}
	printf(" OK (klucz: %d)\n", shm_entries_key);
	
	//Tworzenie segmentu pamieci wspoldzielonej (wpisy)
	printf("[Serwer]: tworze segment pamieci wspolnej na %d wpisow po %ldb...\n", n, sizeof(struct entry));
	if((shm_entries_id= shmget(shm_entries_key, sizeof(struct entry) * n, 0666 | IPC_CREAT))==-1){
		perror("Error shmget()");
		exit(1);
	}
	
	//Pobieranie informacji o pamieci wspoldzielonej
	shmctl(shm_entries_id, IPC_STAT, &buf);
	printf("          OK (id: %d, rozmiar: %zub)\n", shm_entries_id, buf.shm_segsz);
	
	//Tworzenie klucza dla pamieci wspoldzielonej (stan serwera)	
	printf("[Serwer]: tworze klucz dla stanu serwera na podstawie pliku ./%s...\n", argv[1]);
	if((shm_state_key= ftok(argv[1], 'C')) == -1){
		perror("ftok()");
		exit(1);
	}

	//Tworzenie segmentu pamieci wspoldzielonej (stan serwera)
	printf("[Serwer]: tworze segment pamieci wspolnej dla stanu serwera o wielkosci %ldb...\n", sizeof(struct state));	
	if((shm_state_id= shmget(shm_state_key, sizeof(struct state), 0666 | IPC_CREAT))==-1){
		perror("Error shmget()");
		exit(1);
	}
	
	//Tworzenie/uzyskanie listy semaforow
	if((sem_entries_key=ftok(argv[1], 'S')) == -1){
		perror("ftok()");
		exit(1);
	}
	if((sem_entries_id=semget(sem_entries_key,n, 0666 | IPC_CREAT)) == -1){
		perror("semget()");
		exit(1);
	}

	//Tworzenie/uzyskanie semafora licznika wpisow
	if((sem_counter_key=ftok(argv[1], 'C')) == -1){
		perror("ftok()");
		exit(1);
	}
	if((sem_counter_id=semget(sem_counter_key, 1, 0666 | IPC_CREAT)) == -1){
		perror("semget()");
		exit(1);
	}

	//Dolaczenie pamieci wspoldzielonej (wpisy)
	printf("[Serwer]: dolaczam pamiec wspolna dla wpisow...");
	shm_entries= (struct entry *) shmat(shm_entries_id, (void *)0, 0);
	if(shm_entries==(struct entry*)-1){
		perror("Error shmat()");
		exit(1);		
	}

	printf("\n          OK (adres: %p)\n", (void *)shm_entries);

	//Dolaczenie pamieci wspoldzielonej (stan)
	printf("[Serwer]: dolaczam pamiec wspolna dla stanu serwera...");
	shm_state=(struct state *) shmat(shm_state_id, NULL, 0);
	if(shm_state==(struct state*)-1){
		perror("Error shmat()");
		exit(1);
	}

	printf("\n          OK (adres: %p)\n", (void *)shm_state);

	shm_state->counter=0; //Inicjalizacja licznika wpisow
	shm_state->n=n; //Zapis maksymalnej liczby wpisow

	//Inicjalizacja semaforow
	semctl(sem_counter_id, 0, SETVAL, 1);
	semaphores=(int*) malloc(sizeof(int)*n);
	for(int i=0; i<n; i++)
		semctl(sem_entries_id, i, SETVAL, 1);


	printf("[Serwer]: nacisnij CTRL^Z by wyswietlic stan serwisu\n");
	printf("[Serwer]: nacisnij CTRL^C by zakonczyc program\n");

	while(1){
		sleep(1);	
	}
	return 0;
}
