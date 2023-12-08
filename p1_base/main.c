#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include "constants.h"
#include "operations.h"
#include "parser.h"

void errMsg(const char *format) {
    perror(format);
}

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
  const char *dirpath = "jobs";
  DIR *dirp;

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
      errMsg("opendir failed on");
      return 1;
    }
  }

  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  struct dirent *dp;

  pid_t pid = 1;
  while (1) {
      errno = 0; /* To distinguish error from end-of-directory */
      dp = readdir(dirp);
      if (dp == NULL)
          break;

      // Check if the file name ends with ".jobs"
      if (!(strlen(dp->d_name) >= 5 && strcmp(dp->d_name + strlen(dp->d_name) - 5, ".jobs") == 0))
        continue;

      if (pid != 0)
        pid = fork();
      if (pid == -1)
        errMsg("Error creating fork");
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

    int fd = open(file_path, O_RDONLY);
    int output_fd = open(output_file_path, O_CREAT | O_TRUNC | O_WRONLY , S_IRUSR | S_IWUSR);

    int end_of_file = 0;
    while (!end_of_file) {
      unsigned int event_id, delay;
      size_t num_rows, num_columns, num_coords;
      size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

      switch (get_next(fd)) {
        case CMD_CREATE:
          if (parse_create(fd, &event_id, &num_rows, &num_columns) != 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (ems_create(event_id, num_rows, num_columns)) {
            fprintf(stderr, "Failed to create event\n");
          }

          break;

        case CMD_RESERVE:
          num_coords = parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);

          if (num_coords == 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (ems_reserve(event_id, num_coords, xs, ys)) {
            fprintf(stderr, "Failed to reserve seats\n");
          }

          break;

        case CMD_SHOW:
          if (parse_show(fd, &event_id) != 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (ems_show(event_id, output_fd)) {
            fprintf(stderr, "Failed to show event\n");
          }

          break;

        case CMD_LIST_EVENTS:
          if (ems_list_events(output_fd)) {
            fprintf(stderr, "Failed to list events\n");
          }

          break;

        case CMD_WAIT:
          if (parse_wait(fd, &delay, NULL) == -1) {  // thread_id is not implemented
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (delay > 0) {
            printf("Waiting...\n");
            ems_wait(delay);
          }

          break;

        case CMD_INVALID:
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          break;

        case CMD_HELP: {
          char* commands = "Available commands:\n"
              "  CREATE <event_id> <num_rows> <num_columns>\n"
              "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
              "  SHOW <event_id>\n"
              "  LIST\n"
              "  WAIT <delay_ms> [thread_id]\n"  // thread_id is not implemented
              "  BARRIER\n"                      // Not implemented
              "  HELP\n";
          write(output_fd, commands, strlen(commands));
          break;
        }

        case CMD_BARRIER:  // Not implemented
        case CMD_EMPTY:
          break;

        case EOC:
          end_of_file = 1;
          break;
      } 
    }
    ems_terminate();
  }
  else {
    // Waiting for all the child processes to finish
    while (wait(NULL) > 0);
    closedir(dirp);
  }
  return 0;
}
