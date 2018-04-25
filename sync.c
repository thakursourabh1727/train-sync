#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#define SIGALRM 2

int MIN(int x,int y){
	if(x>y)
		return y;
	else
		return x;
}

struct station {
  pthread_mutex_t lock;
  pthread_cond_t is_train_arrived;
  pthread_cond_t is_train_full;
  pthread_cond_t is_passengers_seated;
  int passengers_outside;
  int passengers_inside;
  int empty_seats;
};

struct load_train_args {
	struct station *station;
	int free_seats;
};

volatile int threads_completed = 0;
volatile int load_train_returned = 0;

void init_func(struct station *station)
{
	pthread_mutex_init(&station->lock , NULL);
	pthread_cond_init(&station->is_train_arrived ,NULL);
	pthread_cond_init(&station->is_passengers_seated,NULL);


	station->passengers_inside=0;
    station->passengers_outside = 0;
    station->empty_seats = 0;

}

void load_train_func(struct station *station, int count)
{

    pthread_mutex_lock(&station->lock);

    station->empty_seats = count;

    while(station->empty_seats > 0 && station->passengers_outside > 0){

            pthread_cond_broadcast(&station->is_train_arrived);
            pthread_cond_wait(&station->is_passengers_seated , &station->lock);

    }

    station->empty_seats = 0;
	pthread_mutex_unlock(&station->lock);
}


void train_wait_func(struct station *station)
{
    pthread_mutex_lock(&station->lock);
	station->passengers_outside++;

	while(station->passengers_inside == station->empty_seats){
        pthread_cond_wait(&station->is_train_arrived , &station->lock);
	}

	station->passengers_inside++;
	station->passengers_outside--;
	pthread_mutex_unlock(&station->lock);
}

void on_board_func(struct station *station)
{
    //let the train knows that it is on board......
	pthread_mutex_lock(&station->lock);
	station->passengers_inside--;
	station->empty_seats--;



	if((station->empty_seats == 0) || (station->passengers_inside == 0)){
        pthread_cond_signal(&station->is_passengers_seated);
	}

	pthread_mutex_unlock(&station->lock);

}

void* passenger_thread(void *arg)
{
	struct station *station = (struct station*)arg;
	train_wait_func(station);
	__sync_add_and_fetch(&threads_completed, 1);
	return NULL;
}


void* load_train_thread(void *args)
{
	struct load_train_args *ltargs = (struct load_train_args*)args;
	load_train_func(ltargs->station, ltargs->free_seats);
	load_train_returned = 1;
	return NULL;
}

const char* alarm_error_str;
int alarm_timeout;

void _alarm(int seconds, const char *error_str)
{
	alarm_timeout = seconds;
	alarm_error_str = error_str;
	alarm(seconds);
}

void alarm_handler(int foo)
{
	fprintf(stderr, "Error: Failed to complete after %d seconds. Possible error hint: [%s]\n",
		alarm_timeout, alarm_error_str);
	exit(1);
}

int main()
{
	struct station station;
	init_func(&station);

	srand(getpid() ^ time(NULL));

	signal(SIGALRM, alarm_handler);

	_alarm(1, "load_train_func() did not return immediately when no waiting passengers");
	load_train_func(&station, 0);
	load_train_func(&station, 10);
	_alarm(0, NULL);
	int i;
	const int total_passengers = 100;
	int passengers_left = total_passengers;
	for (i = 0; i < total_passengers; i++) {
		pthread_t tid;
		int ret = pthread_create(&tid, NULL, passenger_thread, &station);
		if (ret != 0) {
			perror("pthread_create");
			exit(1);
		}
	}

	_alarm(2, "load_train_func() did not return immediately when no free seats");
	load_train_func(&station, 0);
	_alarm(0, NULL);

	int total_passengers_boarded = 0;
	const int max_free_seats_per_train = 50;
	int pass = 0;
	while (passengers_left > 0) {
		_alarm(2, "Passengers are unable to board train.");

		int free_seats = rand() % max_free_seats_per_train;

		printf("Train Arriving at station having %d free seats.\n", free_seats);
		load_train_returned = 0;
		struct load_train_args args = { &station, free_seats };
		pthread_t lt_tid;
		int ret = pthread_create(&lt_tid, NULL, load_train_thread, &args);
		if (ret != 0) {
			perror("pthread_create");
			exit(1);
		}

		int threads_to_reap = MIN(passengers_left, free_seats);
		int threads_reaped = 0;
		while (threads_reaped < threads_to_reap) {
			if (load_train_returned) {
				fprintf(stderr, "Error: load_train_func returned early!\n");
				exit(1);
			}
			if (threads_completed > 0) {
				if ((pass % 2) == 0)
					usleep(rand() % 2);
				threads_reaped++;
				on_board_func(&station);
				__sync_sub_and_fetch(&threads_completed, 1);
			}
		}

		for (i = 0; i < 1000; i++) {
			if (i > 50 && load_train_returned)
				break;
			usleep(1000);
		}

		if (!load_train_returned) {
			fprintf(stderr, "Error: load_train_func failed to return\n");
			exit(1);
		}

		while (threads_completed > 0) {
			threads_reaped++;
			__sync_sub_and_fetch(&threads_completed, 1);
		}

		passengers_left -= threads_reaped;
		total_passengers_boarded += threads_reaped;
		printf("Train departed station having %d new passenger(s) (expected %d)%s\n",
			threads_to_reap, threads_reaped,
			(threads_to_reap != threads_reaped) ? " =====" : "");

		if (threads_to_reap != threads_reaped) {
			fprintf(stderr, "Error: Passengers Overloaded!\n");
			exit(1);
		}

		pass++;
	}

	if (total_passengers_boarded == total_passengers) {
		printf("Looks good!\n");
		return 0;
	} else {
		fprintf(stderr, "Error: Expected %d Passengers, but got %d!\n",total_passengers, total_passengers_boarded);
		return 1;
	}
}
