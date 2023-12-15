#include "eventlist.h"
#include <stdio.h>

#include <stdlib.h>

struct EventList* create_list() {
  struct EventList* list = (struct EventList*)malloc(sizeof(struct EventList));
  if (!list) return NULL;
  list->head = NULL;
  list->tail = NULL;
  if (pthread_rwlock_init(&list->list_lock, NULL)) {
    fprintf(stderr, "Lock Error");
    exit(1);
  }
  return list;
}

int append_to_list(struct EventList* list, struct Event* event) {
  if (!list) return 1;

  struct ListNode* new_node = (struct ListNode*)malloc(sizeof(struct ListNode));
  if (!new_node) return 1;

  new_node->event = event;
  new_node->next = NULL;

  if (pthread_rwlock_wrlock(&list->list_lock)) {
    fprintf(stderr, "Lock Error");
    exit(1);
  }
  if (list->head == NULL) {
    list->head = new_node;
    list->tail = new_node;
  } else {
    list->tail->next = new_node; //
    list->tail = new_node;
  }
  if (pthread_rwlock_unlock(&list->list_lock)) {
    fprintf(stderr, "Lock Error");
    exit(1);
  }

  return 0;
}

static void free_event(struct Event* event) {
  if (!event) return;

  free(event->data);
  for (size_t i = 0; i < event->rows * event->cols; i++) {
    if (pthread_rwlock_destroy(&event->seatlocks[i])) {
      fprintf(stderr, "Lock Error");
      exit(1);
    }
  }
  free(event->seatlocks);
  if (pthread_rwlock_destroy(&event->event_lock)) {
    fprintf(stderr, "Lock Error");
    exit(1);
  }
  pthread_mutex_destroy(&event->reservation_lock);
  free(event);
}

void free_list(struct EventList* list) {
  if (!list) return;

  struct ListNode* current = list->head;
  while (current) {
    struct ListNode* temp = current;
    current = current->next;

    free_event(temp->event);
    free(temp);
  }
  if (pthread_rwlock_destroy(&list->list_lock)) {
    fprintf(stderr, "Lock Error");
    exit(1);
  }

  free(list);
}

struct Event* get_event(struct EventList* list, unsigned int event_id) {
  if (!list) return NULL;

  if (pthread_rwlock_rdlock(&list->list_lock)) {
    fprintf(stderr, "Lock Error");
    exit(1);
  }
  struct ListNode* current = list->head;
  while (current) {
    struct Event* event = current->event;
    if (event->id == event_id) {
      if (pthread_rwlock_unlock(&list->list_lock)) {
        fprintf(stderr, "Lock Error");
        exit(1);
      }
      return event;
    }
    current = current->next;//
  }
  if (pthread_rwlock_unlock(&list->list_lock)) {
    fprintf(stderr, "Lock Error");
    exit(1);
  }

  return NULL;
}
