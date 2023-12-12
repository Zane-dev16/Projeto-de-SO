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

int  fd;
int  output_fd;
pthread_mutex_t mutex;
pthread_rwlock_t rwl;
pthread_mutex_t outputlock;

void * process_line() {
      unsigned int event_id, delay;
      size_t num_rows, num_columns, num_coords;
      size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

      pthread_mutex_lock(&mutex);
      switch (get_next(fd)) {
        case CMD_CREATE:
          if (parse_create(fd, &event_id, &num_rows, &num_columns) != 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            // continue;
            return (void*)0;
          }

          if (ems_create(event_id, num_rows, num_columns)) {
            fprintf(stderr, "Failed to create event\n");
          }
          pthread_mutex_unlock(&mutex);

          break;

        case CMD_RESERVE:
          num_coords = parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);

          if (num_coords == 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            //continue;
            return (void*)0;
          }
          pthread_mutex_unlock(&mutex);

          pthread_rwlock_wrlock(&rwl);
          if (ems_reserve(event_id, num_coords, xs, ys)) {
            fprintf(stderr, "Failed to reserve seats\n");
          }
          pthread_rwlock_unlock(&rwl);

          break;

        case CMD_SHOW:
          if (parse_show(fd, &event_id) != 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            //continue;
            return (void*)0;
          }
          pthread_mutex_unlock(&mutex);

          pthread_rwlock_rdlock(&rwl);
          pthread_mutex_lock(&outputlock);
          if (ems_show(event_id, output_fd)) {
            fprintf(stderr, "Failed to show event\n");
          }
          pthread_mutex_unlock(&outputlock);
          pthread_rwlock_unlock(&rwl);

          break;

        case CMD_LIST_EVENTS:
          pthread_mutex_unlock(&mutex);
          pthread_mutex_lock(&outputlock);
          if (ems_list_events(output_fd)) {
            fprintf(stderr, "Failed to list events\n");
          }
          pthread_mutex_unlock(&outputlock);

          break;

        case CMD_WAIT:
          if (parse_wait(fd, &delay, NULL) == -1) {  // thread_id is not implemented
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            //continue;
            return (void*)0;
          }
          pthread_mutex_unlock(&mutex);

          if (delay > 0) {
            printf("Waiting...\n");
            ems_wait(delay);
          }

          break;

        case CMD_INVALID:
          pthread_mutex_unlock(&mutex);
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          break;

        case CMD_HELP: {
          pthread_mutex_unlock(&mutex);
          char* commands = "Available commands:\n"
              "  CREATE <event_id> <num_rows> <num_columns>\n"
              "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
              "  SHOW <event_id>\n"
              "  LIST\n"
              "  WAIT <delay_ms> [thread_id]\n"  // thread_id is not implemented
              "  BARRIER\n"                      // Not implemented
              "  HELP\n";

          pthread_mutex_lock(&outputlock);
          write(output_fd, commands, strlen(commands));
          pthread_mutex_unlock(&outputlock);

          break;
        }

        case CMD_BARRIER:  // Not implemented
          pthread_mutex_unlock(&mutex);
          return (void*)1;
          break;

        case CMD_EMPTY:
          pthread_mutex_unlock(&mutex);
          break;

        case EOC:
          pthread_mutex_unlock(&mutex);
          return  (void*)2;
          break;
      }
    return 0;
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
  printf("%d\n", max_thr);

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

    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_init(&outputlock, NULL);
    pthread_rwlock_init(&rwl, NULL);
    pthread_t th[max_thr];
    for (unsigned int i = 0; i < max_thr; i++) {
      if (pthread_create(&th[i], NULL, process_line, NULL) != 0) {
          fprintf(stderr, "Failed to create thread");
          return 1;
      }
    }
    for (unsigned int i = 0; i < max_thr; i++) {
      if (pthread_join(th[i], NULL) != 0) {
          printf("End of File");
      }
    }
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&outputlock);
    pthread_rwlock_destroy(&rwl);
    // while (!end_of_file) {}
  }
  else {
    // Waiting for all the child processes to finish
    while (wait(NULL) > 0);
  }
  ems_terminate();
  closedir(dirp);
  return 0;
}
