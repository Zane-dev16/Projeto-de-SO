#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <pthread.h>
#include "constants.h"
#include "operations.h"
#include "parser.h"

int fd;
int output_fd;
pthread_mutex_t input_lock;

// global variable for barrier
int terminate_reading;

// global variable for wait
int* wait_times;

void * process_line(void* arg) {

  int *returnValue = malloc(sizeof(int));
  int thread_id = *(int*) arg;
  free(arg);
  while (1) {
    unsigned int event_id, delay, thr_id;
    size_t num_rows, num_columns, num_coords;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

    pthread_mutex_lock(&input_lock);
    if (terminate_reading) {
      pthread_mutex_unlock(&input_lock);
      *returnValue = 0;
      return (void *)returnValue;
    }
    if (wait_times[thread_id]) {                  // sets wait_id to default again
      pthread_mutex_unlock(&input_lock);
      ems_wait((unsigned int)wait_times[thread_id]);
      wait_times[thread_id] = 0;
      pthread_mutex_lock(&input_lock);
    }
    // printf("%d\n", thread_id);
    switch (get_next(fd)) {
      case CMD_CREATE:
        if (parse_create(fd, &event_id, &num_rows, &num_columns) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          *returnValue = 0;
          return (void *)returnValue;
        }

        if (ems_create(event_id, num_rows, num_columns)) {
          fprintf(stderr, "Failed to create event\n");
        }
        pthread_mutex_unlock(&input_lock);

        break;

      case CMD_RESERVE:
        num_coords = parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);
        pthread_mutex_unlock(&input_lock);

        if (num_coords == 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          *returnValue = 0;
          return (void *)returnValue;
        }

        if (ems_reserve(event_id, num_coords, xs, ys)) {
          fprintf(stderr, "Failed to reserve seats\n");
        }
        break;

      case CMD_SHOW:
        if (parse_show(fd, &event_id) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          *returnValue = 0;
          return (void *)returnValue;
        }
        pthread_mutex_unlock(&input_lock);

        if (ems_show(event_id, output_fd)) {
          fprintf(stderr, "Failed to show event\n");
        }

        break;

      case CMD_LIST_EVENTS:
        pthread_mutex_unlock(&input_lock);

        if (ems_list_events(output_fd)) {
          fprintf(stderr, "Failed to list events\n");
        }

        break;

      case CMD_WAIT:
        if (parse_wait(fd, &delay, &thr_id) == -1) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          *returnValue = 0;
          return (void *)returnValue;
        }

        if (delay > 0) {
          // printf("Waiting...%d\n", thread_id);             // *****************************************************
          if (thr_id != 0) {
            wait_times[thr_id] = (int)delay;
          }
          else
            ems_wait(delay);
        }
        pthread_mutex_unlock(&input_lock);
        break;

      case CMD_INVALID:
        pthread_mutex_unlock(&input_lock);
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        break;

      case CMD_HELP: {
        pthread_mutex_unlock(&input_lock);
        // PRINTAR NO TERMINAL **********************************************************************************
        printf("Available commands:\n"
            "  CREATE <event_id> <num_rows> <num_columns>\n"
            "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
            "  SHOW <event_id>\n"
            "  LIST\n"
            "  WAIT <delay_ms> [thread_id]\n"
            "  BARRIER\n"
            "  HELP\n"
        );

        break;
      }

      case CMD_BARRIER:
        terminate_reading = 1;
        *returnValue = 1;
        pthread_mutex_unlock(&input_lock);
        return (void *)returnValue;

      case CMD_EMPTY:
        pthread_mutex_unlock(&input_lock);
        break;

      case EOC:
        terminate_reading = 1;
        *returnValue = 0;
        pthread_mutex_unlock(&input_lock);
        return (void *)returnValue;
    }
  }
}

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
  const char *dirpath = "jobs";
  DIR *dirp;
  unsigned int max_proc = 0;
  unsigned int max_thr = 0;

  if (argc > 1) {
    char *endptr;
    unsigned long int delay = strtoul(argv[1], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }
    state_access_delay_ms = (unsigned int)delay;
  }
  if (argc > 2) {
    dirpath = argv[2];
    dirp = opendir(dirpath);
    if (dirp == NULL) {
      fprintf(stderr, "Open dir failed\n");
      return 1;
    }
  }
  if (argc > 3) {
    char *endptr;
    unsigned long int arg3 = strtoul(argv[3], &endptr, 10);

    if (*endptr != '\0' || arg3 > UINT_MAX || arg3 == 0) {
      fprintf(stderr, "Invalid max process value or value too large\n");
      return 1;
    }
    max_proc = (unsigned int)arg3;
  }
  if (argc > 4) {
    char *endptr;
    unsigned long int arg4 = strtoul(argv[4], &endptr, 10);

    if (*endptr != '\0' || arg4 > UINT_MAX || arg4 == 0) {
      fprintf(stderr, "Invalid max process value or value too large\n");
      return 1;
    }
    max_thr = (unsigned int)arg4;
  }

  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  struct dirent *dp;
  pid_t pid = 1;
  unsigned int num_proc = 0;
  while (1) {
    errno = 0; /* To distinguish error from end-of-directory */
    dp = readdir(dirp);
    if (dp == NULL)
        break;

    // Check if the file name ends with ".jobs"
    if (!(strlen(dp->d_name) >= 5 && strcmp(dp->d_name + strlen(dp->d_name) - 5, ".jobs") == 0))
      continue;

    if (num_proc == max_proc) {
      wait(NULL);
      num_proc--;
    }
    if (pid != 0) {
      num_proc++;
      pid = fork();
    }
    if (pid == -1)
      fprintf(stderr, "Error creating fork\n");
    if (pid == 0)
      break;
  }

  if (pid == 0) {
    char file_path[256];
    strcpy(file_path, dirpath);
    strcat(file_path, "/");
    strcat(file_path, dp->d_name);

    char output_file_path[256];
    strcpy(output_file_path, dirpath);
    strcat(output_file_path, "/");
    strncat(output_file_path, dp->d_name, strlen(dp->d_name) - 4);
    strcat(output_file_path, "out");

    fd = open(file_path, O_RDONLY);
    output_fd = open(output_file_path, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);

    pthread_mutex_init(&input_lock, NULL);
    pthread_t th[max_thr];
    wait_times = (int*)malloc((max_thr + 1) * sizeof(int));
    
    int barrier_found = 1;
    while (barrier_found) {
      terminate_reading = 0;
      barrier_found = 0;
      for (unsigned int i = 0; i < max_thr; i++) {
        unsigned int* thread_id = malloc(sizeof(int));
        *thread_id = i + 1;
        wait_times[*thread_id] = 0;
        if (pthread_create(&th[i], NULL, process_line, thread_id) != 0) {
            fprintf(stderr, "Failed to create thread");
            return 1;
        }
      }
      void * status = 0;
      for (unsigned int i = 0; i < max_thr; i++) {
        if (pthread_join(th[i], &status) != 0) {
            fprintf(stderr, "Failed to create thread");
            return 1;
        }
        if (*((int*)status) == 1) {
          barrier_found = 1;
        }
        free(status);
      }
    }
    free(wait_times);
    pthread_mutex_destroy(&input_lock);

  }
  else {
    // Waiting for all the child processes to finish
    while (wait(NULL) > 0);
  }
  ems_terminate();
  closedir(dirp);
  return 0;
}