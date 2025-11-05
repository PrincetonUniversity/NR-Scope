#include "srsran/phy/ue/srsgui_plot.h"

typedef struct node {
   int M;
   cf_t *symbols;
   struct node *next;
} node_t;

void enqueue(node_t **head, int M, cf_t *symbols) {
  node_t *new_node = (node_t*)malloc(sizeof(*new_node));
  if (!new_node) {
    perror("malloc new_node");
    return;
  }
  new_node->M = M;
  new_node->symbols = (cf_t*)malloc((size_t)M * sizeof(cf_t));
  if (!new_node->symbols) {
    perror("malloc symbols");
    free(new_node);
    return;
  }
  memcpy(new_node->symbols, symbols, (size_t)M * sizeof(cf_t));
  new_node->next = *head;
  *head = new_node;
}

node_t dequeue(node_t **head) {
  node_t out = {0};                 // empty/sentinel result
  node_t *current, *prev = NULL;

  if (*head == NULL) return out;

  current = *head;
  while (current->next != NULL) {
    prev = current;
    current = current->next;
  }

  out.M       = current->M;
  out.symbols = current->symbols;
  out.next    = NULL;
  free(current);                        // free the node structure only

  if (prev)
    prev->next = NULL;
  else
    *head = NULL;

  return out;
}

pthread_t plot_thread;
sem_t     plot_sem;
pthread_mutex_t plot_lock; // lock for the queue 
node_t *data_queue;
bool initialized = false;
plot_scatter_t pscatequal;

void push_node(cf_t *symbols, int M) {
  if (!initialized) return;

  pthread_mutex_lock(&plot_lock);
  enqueue(&data_queue, M, symbols);
  pthread_mutex_unlock(&plot_lock);
  sem_post(&plot_sem);
}

void* plot_thread_run(void* arg)
{
  sdrgui_init();

  plot_scatter_init(&pscatequal);
  plot_scatter_setTitle(&pscatequal, "PDCCH Symbol Constellation Plot");
  plot_scatter_setXAxisScale(&pscatequal, -3, 3);
  plot_scatter_setYAxisScale(&pscatequal, -3, 3);

  plot_scatter_addToWindowGrid(&pscatequal, (char*)"pdcch", 0, 0);

  while (1) {
    sem_wait(&plot_sem);
    pthread_mutex_lock(&plot_lock);
    node_t a = dequeue(&data_queue);     // <-- guarded by the same lock
    pthread_mutex_unlock(&plot_lock);
    plot_scatter_setNewData(&pscatequal, a.symbols, a.M);
  }
  return NULL;
}

void init_plots() {
  if (sem_init(&plot_sem, 0, 0)) {
    perror("sem_init");
    exit(-1);
  }

  pthread_attr_t     attr;
  struct sched_param param;
  param.sched_priority = 0;
  data_queue = NULL;
  pthread_attr_init(&attr);
  pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
  pthread_attr_setschedparam(&attr, &param);
  if (pthread_create(&plot_thread, NULL, plot_thread_run, NULL)) {
    perror("pthread_create");
    exit(-1);
  }
  initialized = true;
}
