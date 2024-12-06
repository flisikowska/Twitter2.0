#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
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

struct entry *shm_entries;
struct state *shm_state;

int main(int argc, char* argv[]){
	char bufor[MY_MSG_SIZE];
	
	//Tworzenie klucza i uzyskanie dostepu do segmentu pamieci wspoldzielonej (wpisy)
	if((shm_entries_key= ftok(argv[1], 'R'))==-1){
		perror("ftok()");
		exit(1);
	}
	if((shm_entries_id= shmget(shm_entries_key, 0, 0))==-1){
		perror("Error shmget()");
		exit(1);
	}

	//Tworzenie klucza i uzyskanie dostepu do segmentu pamieci wspoldzielonej (stan serwera)
	if((shm_state_key= ftok(argv[1], 'C'))==-1){
		perror("ftok()");
		exit(1);
	}
	if((shm_state_id= shmget(shm_state_key, 0, 0))==-1){
		perror("Error shmget()");
		exit(1);
	}

	//Dolaczenie pamieci wspolnej (wpisy)
	shm_entries= (struct entry *) shmat(shm_entries_id, (void *)0, 0);
	if(shm_entries==(struct entry*)-1){
		perror("Error shmat()");
		exit(1);		
	}

	//Dolaczenie pamieci wspolnej (stan serwera)
	shm_state=(struct state*) shmat(shm_state_id, NULL, 0);
	if(shm_state==(struct state*)-1){
		perror("Error shmat()");
		exit(1);		
	}
	
	//Tworzenie klucza i uzyskanie dostepu do tablicy semaforow
	sem_entries_key=ftok(argv[1], 'S');
        if((sem_entries_id=semget(sem_entries_key, shm_state->n, 0666)) == -1) {
                perror("Error semget()");
                exit(1);
        }

	//Tworzenie klucza i uzyskanie dostepu do semafora licznika
        sem_counter_key=ftok(argv[1], 'C');
        if((sem_counter_id=semget(sem_counter_key, 1, 0666)) == -1) {
                perror("Error semget()");
                exit(1);
        }


	printf("Twitter 2.0 wita! (wersja C)\n");

	struct sembuf sb;
	struct sembuf sb_counter;

	//Obsluga dodania nowego wpisu
	if(argv[2][0]=='N'){
		if(strlen(argv[3])>LOGIN_SIZE){
			fprintf(stderr, "Login jest zbyt dlugi\n");
			exit(1);
		}

		printf("[Wolnych %d wpisow(na %d)]\n", (shm_state->n)-(shm_state->counter), shm_state->n);
		printf("Podaj swoj wpis: \n");
		int count=read(0, bufor, MY_MSG_SIZE);
		if(count>=MY_MSG_SIZE){
			fprintf(stderr, "Wiadomosc jest zbyt dluga\n");
			exit(1);
		}

		//Zablokowanie licznika wpisow
		sb_counter.sem_num=0;
		sb_counter.sem_op=-1;
		sb_counter.sem_flg=0;
		semop(sem_counter_id, &sb_counter, 1);
		
		//Aktualizacja licznika wpisow
		int current_counter= shm_state->counter;
		shm_state->counter=current_counter+1;

		//Odblokowanie licznika wpisow
		sb_counter.sem_op=1;
		semop(sem_counter_id, &sb_counter, 1);
		
		if(current_counter<=shm_state->n){
			//Zablokowanie miejsca na nowy wpis
			sb.sem_num=current_counter;
			sb.sem_op=-1;
			sb.sem_flg=0;
			semop(sem_entries_id, &sb, 1);

			//Zapis nowego wpisu w pamieci wspoldzielonej
			strncpy(shm_entries[current_counter].username, argv[3], LOGIN_SIZE);
			strncpy(shm_entries[current_counter].message, bufor, count);
			shm_entries[current_counter].message[count-1]='\0';
			shm_entries[current_counter].likes=0;

			//Odblokowanie miejsca na nowy wpis
			sb.sem_op=1;
			semop(sem_entries_id, &sb, 1);
		}
		else{
			printf("Brak miejsca na nowy wpis");
			exit(0);
		}
	}
	//Obsluga polubienia istniejacego wpisu
	else if(argv[2][0]=='P'){
		int wpisDoPolubienia;
		printf("Wpisy w serwisie:\n");
		for(int i=0; i<shm_state->counter; i++)
			printf("    %d. %s [Autor: %s, Polubienia: %d]\n", i+1, shm_entries[i].message, shm_entries[i].username, shm_entries[i].likes);
		printf("Ktory wpis chcesz polubic\n");
		scanf("%d", &wpisDoPolubienia);
		if(wpisDoPolubienia>shm_state->counter || wpisDoPolubienia<1){
			printf("Niepoprawna liczba\n");
			exit(1);
		}
		
		//Zablokowanie wybranego wpisu
		sb.sem_num=wpisDoPolubienia -1;
		sb.sem_op=-1;
		sb.sem_flg=0;
		semop(sem_entries_id, &sb, 1);
		
		//Zwiekszenie licznika polubien
		shm_entries[wpisDoPolubienia-1].likes++;

		//Odblokowanie wybranego wpisu
		sb.sem_op=1;
		semop(sem_entries_id, &sb, 1);
	}
	
	printf("Dziekujemy za skorzystanie z aplikacji Twitter 2.0\n");
	return 0;
}
