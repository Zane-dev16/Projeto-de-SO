#include "eventlist.h"

#include <stdlib.h>

struct EventList* create_list() {
  struct EventList* list = (struct EventList*)malloc(sizeof(struct EventList));
  if (!list) return NULL;
  list->head = NULL;
  list->tail = NULL;
  pthread_rwlock_init(&list->list_lock, NULL);
  return list;
}

int append_to_list(struct EventList* list, struct Event* event) {
  if (!list) return 1;

  struct ListNode* new_node = (struct ListNode*)malloc(sizeof(struct ListNode));
  if (!new_node) return 1;

  new_node->event = event;
  new_node->next = NULL;

  pthread_rwlock_wrlock(&list->list_lock);
  if (list->head == NULL) {
    list->head = new_node;
    list->tail = new_node;
  } else {
    list->tail->next = new_node; //
    list->tail = new_node;
  }
  pthread_rwlock_unlock(&list->list_lock);

  return 0;
}

static void free_event(struct Event* event) {
  if (!event) return;

  free(event->data);
  for (size_t i = 0; i < event->rows * event->cols; i++) {
    pthread_rwlock_destroy(&event->seatlocks[i]);
  }
  free(event->seatlocks);
  pthread_rwlock_destroy(&event->event_lock);
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
  pthread_rwlock_destroy(&list->list_lock);

  free(list);
}

struct Event* get_event(struct EventList* list, unsigned int event_id) {
  if (!list) return NULL;

  pthread_rwlock_rdlock(&list->list_lock);
  struct ListNode* current = list->head;
  while (current) {
    struct Event* event = current->event;
    if (event->id == event_id) {
      pthread_rwlock_unlock(&list->list_lock);
      return event;
    }
    current = current->next;//
  }
  pthread_rwlock_unlock(&list->list_lock);

  return NULL;
}
