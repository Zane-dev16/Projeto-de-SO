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

#define MAX_PATH_LENGTH 256
#define ERROR 5

int jobs_fd;
int output_fd;
int terminate_reading;
int* wait_times;
pthread_mutex_t input_lock;

typedef struct {
    unsigned int thread_id;
    unsigned int max_thr;
} thr_args;

void * process_line(void* arg) {
  thr_args const *args = (thr_args const *)arg;

  int *returnValue = malloc(sizeof(int));
  if (returnValue == NULL) {
    fprintf(stderr, "Failed to allocate memory for return value");
    exit(1);
  }
  unsigned int thread_id = args->thread_id;
  unsigned int max_thr = args->max_thr;
  free(arg);
  while (1) {
    unsigned int event_id, delay, thr_id;
    size_t num_rows, num_columns, num_coords;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

    if(pthread_mutex_lock(&input_lock)) {
      fprintf(stderr, "Lock Error\n"); 
      exit(1);
    }
    if (terminate_reading) {
      if(pthread_mutex_unlock(&input_lock)) {
        fprintf(stderr, "Lock Error\n"); 
        exit(1);
      }
      *returnValue = 0;
      return (void *)returnValue;
    }
    while (wait_times[thread_id]) {
      unsigned int wait_time = (unsigned int)wait_times[thread_id];
      wait_times[thread_id] = 0;
      if(pthread_mutex_unlock(&input_lock)) {
        fprintf(stderr, "Lock Error\n"); 
        exit(1);
      }
      ems_wait(wait_time);
      if(pthread_mutex_lock(&input_lock)) {
        fprintf(stderr, "Lock Error\n");
        exit(1);
      }
    }
    switch (get_next(jobs_fd)) {
      case CMD_CREATE:
        if (parse_create(jobs_fd, &event_id, &num_rows, &num_columns) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          *returnValue = 0;
          return (void *)returnValue;
        }

        if (ems_create(event_id, num_rows, num_columns)) {
          fprintf(stderr, "Failed to create event\n");
        }
        if(pthread_mutex_unlock(&input_lock)) {
          fprintf(stderr, "Lock Error\n"); 
          exit(1);
        }

        break;

      case CMD_RESERVE:
        num_coords = parse_reserve(jobs_fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);
        if(pthread_mutex_unlock(&input_lock)) {
          fprintf(stderr, "Lock Error\n"); 
          exit(1);
        }

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
        if (parse_show(jobs_fd, &event_id) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          *returnValue = 0;
          return (void *)returnValue;
        }
        if(pthread_mutex_unlock(&input_lock)) {
          fprintf(stderr, "Lock Error\n"); 
          exit(1);
        }

        if (ems_show(event_id, output_fd)) {
          fprintf(stderr, "Failed to show event\n");
        }

        break;

      case CMD_LIST_EVENTS:
        if(pthread_mutex_unlock(&input_lock)) {
          fprintf(stderr, "Lock Error\n"); 
          exit(1);
        }

        if (ems_list_events(output_fd)) {
          fprintf(stderr, "Failed to list events\n");
        }

        break;

      case CMD_WAIT:
        if (parse_wait(jobs_fd, &delay, &thr_id) == -1) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          *returnValue = 0;
          return (void *)returnValue;
        }

        if (delay > 0) {
          fprintf(stderr, "Waiting...\n");
          if (thr_id != 0) {
            wait_times[thr_id] += (int)delay;
          } 
          else {
            for (unsigned int i = 1; i <= max_thr; i++) {
              wait_times[i] += (int)delay;
            }
          }
        }
        if(pthread_mutex_unlock(&input_lock)) {
          fprintf(stderr, "Lock Error\n"); 
          exit(1);
        }
        break;

      case CMD_INVALID:
      if(pthread_mutex_unlock(&input_lock)) {
      fprintf(stderr, "Lock Error\n"); 
      exit(1);
    };
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        break;

      case CMD_HELP: {
        if(pthread_mutex_unlock(&input_lock)) {
          fprintf(stderr, "Lock Error\n"); 
          exit(1);
        }
        char* commands =  "Available commands:\n"
                          "  CREATE <event_id> <num_rows> <num_columns>\n"
                          "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
                          "  SHOW <event_id>\n"
                          "  LIST\n"
                          "  WAIT <delay_ms> [thread_id]\n"
                          "  BARRIER\n"
                          "  HELP\n";
        write(output_fd, commands, strlen(commands));
        break;
      }

      case CMD_BARRIER:
        terminate_reading = 1;
        *returnValue = 1;
        if(pthread_mutex_unlock(&input_lock)) {
          fprintf(stderr, "Lock Error\n"); 
          exit(1);
        }
        return (void *)returnValue;

      case CMD_EMPTY:
        if(pthread_mutex_unlock(&input_lock)) {
          fprintf(stderr, "Lock Error\n"); 
          exit(1);
        }
        break;

      case EOC:
        terminate_reading = 1;
        *returnValue = 0;
        if(pthread_mutex_unlock(&input_lock)) {
          fprintf(stderr, "Lock Error\n"); 
          exit(1);
        }
        return (void *)returnValue;
    }
  }
}

int openJobsFile(const char *dirpath, const char *filename) {
    char file_path[MAX_PATH_LENGTH];
    snprintf(file_path, sizeof(file_path), "%s/%s", dirpath, filename);

    jobs_fd = open(file_path, O_RDONLY);
    if (jobs_fd == -1) {
      return 1;
    }
    return 0;
}

int openOutputFile(const char *dirpath, const char *filename) {
    char output_file_path[MAX_PATH_LENGTH];
    snprintf(output_file_path, sizeof(output_file_path), "%s/%.*sout", dirpath, (int)(strlen(filename) - 4), filename);
    output_fd = open(output_file_path, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
    if (output_fd == -1) {
      return 1;
    }
    return 0;
}

int parseValue(unsigned int *value, const char *arg) {
    char *endptr;
    unsigned long int val = strtoul(arg, &endptr, 10);

    if (*endptr != '\0' || val > UINT_MAX || val == 0) {
        return 1;
    }

    *value = (unsigned int)val;
    return 0;
}

int is_jobs_file(const char *filename) {
    return !(strlen(filename) >= 5 && strcmp(filename + strlen(filename) - 5, ".jobs") == 0);
}

int init_globals(unsigned int max_thr, const char *dirpath, const char *filename) {
  if(pthread_mutex_init(&input_lock, NULL)) {
    fprintf(stderr, "Mutex initialization failed\n");
    return 1;
  }
  wait_times = (int*)malloc((max_thr + 1) * sizeof(int));
  if (wait_times == NULL) {
    fprintf(stderr, "Failed to initialize wait_times\n");
    return 1;
  }
  if (openJobsFile(dirpath, filename)) {
    fprintf(stderr, "Failed to open jobs file\n");
    return 1;
  }
  if (openOutputFile(dirpath, filename)) {
    fprintf(stderr, "Failed to open output file\n");
    return 1;
  }
  return 0;
}

void terminate_globals() {
  free(wait_times);
  if(pthread_mutex_destroy(&input_lock)) {
    fprintf(stderr, "Lock Error\n"); 
    exit(1);
  }
}

int process_file(unsigned int max_thr) {
    pthread_t th[max_thr];
    int barrier_found = 1;
    while (barrier_found) {
      terminate_reading = 0;
      barrier_found = 0;
      for (unsigned int i = 0; i < max_thr; i++) {
        wait_times[i + 1] = 0;
      }
      for (unsigned int i = 0; i < max_thr; i++) {
        thr_args *args = malloc(sizeof(thr_args));
        if (args == NULL) {
          fprintf(stderr, "Failed to allocate memory for thread_id");
          return 1;
        }
        args->thread_id = i+1;
        args->max_thr = max_thr;
        if (pthread_create(&th[i], NULL, process_line, args) != 0) {
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
    return 0;
}

void wait_for_children() {
    while (wait(NULL) > 0);
}

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
  const char *dirpath = "jobs";
  DIR *dirp;
  unsigned int max_proc = 0;
  unsigned int max_thr = 0;
  struct dirent *dp;
  pid_t pid = 1;
  unsigned int num_proc = 0;


  if (argc > 1) {
    dirpath = argv[1];
    dirp = opendir(dirpath);
    if (dirp == NULL) {
      fprintf(stderr, "Open dir failed\n");
      return 1;
    }
  }
  if (argc > 2) {
    if (parseValue(&max_proc, argv[2])) {
      fprintf(stderr, "Invalid max process value or value too large\n");
      return 1;
    }
  }
  if (argc > 3) {
    if (parseValue(&max_thr, argv[3])) {
      fprintf(stderr, "Invalid max thread value or value too large\n");
      return 1;
    }
  }
  if (argc > 4) {
    if (parseValue(&state_access_delay_ms, argv[4])) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }
  }
  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  while (1) {
    errno = 0;
    dp = readdir(dirp);
    if (dp == NULL)
      break;
    if (is_jobs_file(dp->d_name))
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
    if (init_globals(max_thr, dirpath, dp->d_name)){
      exit(1);
    }
    if(process_file(max_thr)) {
      exit(1);
    }
    terminate_globals();
  }
  else {
    wait_for_children();
  }
  ems_terminate();
  closedir(dirp);
  return 0;
}