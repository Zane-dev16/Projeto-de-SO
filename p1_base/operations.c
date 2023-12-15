#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <string.h>


#include "eventlist.h"

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_ms = 0;

/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event_id The ID of the event to get.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event* get_event_with_delay(unsigned int event_id) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return get_event(event_list, event_id);
}

/// Gets the seat with the given index from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event Event to get the seat from.
/// @param index Index of the seat to get.
/// @return Pointer to the seat.
static unsigned int* get_seat_with_delay(struct Event* event, size_t index) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return &event->data[index];
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }

int ems_init(unsigned int delay_ms) {
  if (event_list != NULL) {
    fprintf(stderr, "EMS state has already been initialized\n");
    return 1;
  }

  event_list = create_list();
  state_access_delay_ms = delay_ms;

  return event_list == NULL;
}

int ems_terminate() {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  free_list(event_list);
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {

  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (get_event_with_delay(event_id) != NULL) {
    fprintf(stderr, "Event already exists\n");
    return 1;
  }

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    fprintf(stderr, "Error allocating memory for event\n");
    return 1;
  }

  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;
  event->data = malloc(num_rows * num_cols * sizeof(unsigned int));
  event->seatlocks = malloc (num_rows * num_cols * sizeof(pthread_mutex_t));
  pthread_rwlock_init(&event->event_lock, NULL);

  if (event->data == NULL) {
    fprintf(stderr, "Error allocating memory for event data\n");
    free(event);
    return 1;
  }

  for (size_t i = 0; i < num_rows * num_cols; i++) {
    event->data[i] = 0;
    pthread_mutex_init(&event->seatlocks[i], NULL);
  }

  if (append_to_list(event_list, event) != 0) {
    fprintf(stderr, "Error appending event to list\n");
    free(event->data);
    free(event);
    return 1;
  }
  return 0;
}


int bubble_sort_seats(size_t arr1[], size_t arr2[], size_t n) {
    for (size_t i = 0; i < n - 1; ++i) {
        size_t swapped = 0; // Flag to check if any elements are swapped
        for (size_t j = 0; j < n - i - 1; ++j) {
            // Swap adjacent elements if they are in the wrong order
            if (arr1[j] > arr1[j + 1]) {
                size_t temp = arr1[j];
                arr1[j] = arr1[j + 1];
                arr1[j + 1] = temp;

                temp = arr2[j];
                arr2[j] = arr2[j + 1];
                arr2[j + 1] = temp;
                swapped = 1; // Set swapped flag if a swap occurs
            }
            else if (arr1[j] == arr1[j + 1]) {
              if (arr2[j] == arr2[j + 1]) {
                return 1;
              }
              if (arr2[j] > arr2[j + 1]) {
                size_t temp = arr2[j];
                arr2[j] = arr2[j + 1];
                arr2[j + 1] = temp;
                swapped = 1; // Set swapped flag if a swap occurs
              }
            }
        }
        // If no two elements were swapped in the inner loop, array is sorted
        if (swapped == 0) {
            break;
        }
    }
    return 0;
}


int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {

  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  if(bubble_sort_seats(xs, ys, num_seats)) {
    fprintf(stderr, "Seat already reserved\n");
    return 1;
  }

  pthread_rwlock_rdlock(&event->event_lock);

  size_t i = 0;
  int can_reserve = 1;
  for (; i < num_seats; i++) {
    size_t row = xs[i];
    size_t col = ys[i];

    if (row <= 0 || row > event->rows || col <= 0 || col > event->cols) {
      can_reserve = 0;
      fprintf(stderr, "Invalid seat\n");
      break;
    }
    pthread_mutex_lock(&event->seatlocks[seat_index(event, xs[i], ys[i])]);
    if (*get_seat_with_delay(event, seat_index(event, row, col)) != 0) {
      can_reserve = 0;
      fprintf(stderr, "Seat already reserved\n");
      break;
    }
  }
  if (can_reserve) {
    unsigned int reservation_id = ++event->reservations;
    for (size_t j = 0; j < num_seats; j++) {
      size_t row = xs[j];
      size_t col = ys[j];
      *get_seat_with_delay(event, seat_index(event, row, col)) = reservation_id;
      pthread_mutex_unlock(&event->seatlocks[seat_index(event, row, col)]);
    }
    pthread_rwlock_unlock(&event->event_lock);
    return 0;
  }
  else {
    for (size_t j = 0; j <= i; j++) {
      size_t row = xs[j];
      size_t col = ys[j];
      pthread_mutex_unlock(&event->seatlocks[seat_index(event, row, col)]);
    }
    pthread_rwlock_unlock(&event->event_lock);
    return 1;
  }
}

int ems_show(unsigned int event_id, int fd) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id);


  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }
  pthread_rwlock_wrlock(&event->event_lock);

  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {
      unsigned int* seat = get_seat_with_delay(event, seat_index(event, i, j));
      char seatStr[12];
      snprintf(seatStr, sizeof(seatStr), "%u", *seat);
      write(fd, seatStr, strlen(seatStr));

      if (j < event->cols) {
        write(fd, " ", strlen(" "));
      }
    }

    write(fd, "\n", strlen("\n"));
  }
  pthread_rwlock_unlock(&event->event_lock);

  return 0;
}

int ems_list_events(int fd) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (event_list->head == NULL) {
    write(fd, "No events\n", strlen("No events\n"));
    return 0;
  }

  struct ListNode* current = event_list->head;
  while (current != NULL) {
    write(fd, "Event: ", strlen("Event: "));
    char event_id_str[12];
    snprintf(event_id_str, sizeof(event_id_str), "%u", (current->event)->id);
    write(fd, event_id_str, strlen(event_id_str));
    write(fd, "\n", strlen("\n"));
    current = current->next;
  }

  return 0;
}

void ems_wait(unsigned int delay_ms) {
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}
