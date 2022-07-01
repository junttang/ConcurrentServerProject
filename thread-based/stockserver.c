/**************************************************
 * Title: SP-Project 2  -  Thread-Based StockServer
 * Summary: 'Thread-Based Concurrent Stock Server'
 for studying the concepts of network programming,
 thread programming, synchronization, semaphore, P-
 -roducer/Consumer Problem, First Readers/Writers
 Problem, etc.
 *  |Date              |Author             |Version
	|2022-05-21        |Park Junhyeok      |1.0.0
**************************************************/

/****************** Declaration ******************/
/* Headers */
#include "csapp.h"


/* Preprocessor Directives */
#define MAX_ITEM	1000000			/* size of pointer array */
#define SBUFSIZE	1000			/* size of shared buffer */
#define NTHREADS	1000			/* number of worker threads */


/* Types */
typedef struct item {				/* node structure of AVL tree */
	int ID;							// ID, left_stock, price : attributes of stock item
	int left_stock;
	int price;
	int height;						// balance factor of node (for AVL operations)
	int readcnt;					// number of Readers who access the node
	sem_t mutex, w;					// semaphores for 'First Readers-Writers Problem'
	struct item *right;				// left and right link of node
	struct item *left;
}Item;

typedef struct {					/* structure for 'Producer-Consumer Problem' */
	int *buf;	 					// shared buffer pointer
	int n; 							// maximum number of slots
	int front; 						// buf[(front+1)%n] (pointing the first item)
	int rear; 						// buf[rear%n] (pointing the last item)
	sem_t mutex; 					// provides mutual exclusion for accessing buffer
	sem_t slots; 					// number of available slots
	sem_t items; 					// number of available items
} sbuf_t;

typedef enum {						/* enumeration for choosing the type of service */
	_show_, _buy_, _sell_, _exit_, _error_
}command;


/* Global Variables */
Item *root = NULL;					/* root of AVL tree */
Item *print[MAX_ITEM];				/* pointer array for 'show' routine, etc */
int print_size;						/* size of pointer array */

sbuf_t sbuf;						/* shared buffer for 'Producer-Consumer Problem */

char buy_success_msg[] = "[buy] success\n";
char buy_error_msg[] = "Not enough left stock\n";
char sell_success_msg[] = "[sell] success\n";
char error_msg[] = "Invalid Command\n";
char exit_msg[] = "exit";			/* these are global strings for providing service */


/* Subroutines for the AVL Tree */
Item* InsertTree(Item*, int, int, int);
Item* SearchTree(Item* node, int id);
void ClearTree(Item* node);
Item* SingleRotateLeft(Item *nodeB);
Item* SingleRotateRight(Item *nodeA);
Item* DoubleRotateLeft(Item *node);
Item* DoubleRotateRight(Item *node);
int GetHeight(Item *node);
int GetGreater(int, int);


/* Subroutines for 'Producer-Consumer Problem' */
void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);


/* Subroutines for Service of Stock Server */
command what_command(char *buf, int *id, int *amount);
void service(int connfd, char *buf, int n);
void show_routine(int connfd);
void buy_routine(int connfd, int id, int amount);
void sell_routine(int connfd, int id, int amount);
void exit_routine(int connfd);
void error_routine(int connfd);
void stock_load(void);
void stock_store(void);
void *thread(void *vargp);
void sigint_handler(int sig);


/**************** Implementation *****************/
/**   Subroutines for Service of Stock Server   **/
/* Main thread (Master/Producer thread of 'Producer-Consumer Problem') */
int main(int argc, char **argv) {
	int listenfd, connfd;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	char client_hostname[MAXLINE], client_port[MAXLINE];
	pthread_t tid;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}
	stock_load();							// load the 'stock.txt', and construct tree
	Signal(SIGINT, sigint_handler);			// install the SIGINT handler

	listenfd = Open_listenfd(argv[1]);
	sbuf_init(&sbuf, SBUFSIZE);
	for (int i = 0; i < NTHREADS; i++)
		Pthread_create(&tid, NULL, thread, NULL);  	// spawn worker threads (consumer)

	while (1) {
		clientlen = sizeof(struct sockaddr_storage);
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
		printf("Connected to (%s, %s)\n", client_hostname, client_port);
		sbuf_insert(&sbuf, connfd);
	}

	exit(0);
}

/* Get and analyze the requests of clients */
command what_command(char *buf, int *id, int *amount) {
	char argument[10];
	if (buf[0] == '\n')
		return _error_;

	sscanf(buf, "%s %d %d", argument, id, amount);

	if (!strcmp(argument, "show"))
		return _show_;
	if (!strcmp(argument, "exit"))				// this function does not check
		return _exit_;								// some errorneous inputs!
	if (!strcmp(argument, "buy"))				// (assume every request is proper)
		return _buy_;
	if (!strcmp(argument, "sell"))
		return _sell_;
	return _error_;
}

/* Choose task based on the type of request */
void service(int connfd, char *buf, int n) {
	int id, amount;

	switch (what_command(buf, &id, &amount)) {		// call by reference for id, amount
	case _show_: show_routine(connfd); break;
	case _buy_: buy_routine(connfd, id, amount); break;
	case _sell_: sell_routine(connfd, id, amount); break;
	case _exit_: exit_routine(connfd); break;
	case _error_: error_routine(connfd); break;
	}
}

/* Routine for 'show' service (routine of 'Reader') */
void show_routine(int connfd) {
	char printbuf[MAXLINE] = "";

	for (int i = 0; i < print_size; i++) {
		char s1[128], s2[128], s3[128];

		P(&(print[i]->mutex));				// mutual exclusion for the present item
		(print[i]->readcnt)++;				// increment the number of readers
		if (print[i]->readcnt == 1)			// if there's at least one reader, then
			P(&(print[i]->w));				// blocking every writer!
		V(&(print[i]->mutex));

		rio_itoa(print[i]->ID, s1, 10);
		rio_itoa(print[i]->left_stock, s2, 10);		// transform integer into string
		rio_itoa(print[i]->price, s3, 10);

		strcat(printbuf, s1); strcat(printbuf, " ");
		strcat(printbuf, s2); strcat(printbuf, " ");
		strcat(printbuf, s3); strcat(printbuf, "\n");

		P(&(print[i]->mutex));
		(print[i]->readcnt)--;
		if (print[i]->readcnt == 0)			// allow writers to do their tasks only if
			V(&(print[i]->w));				// there's no readers!
		V(&(print[i]->mutex));
	}

	Rio_writen(connfd, printbuf, MAXLINE);	// write routine is not under the exclusion
}

/* Routine for 'buy' service (routine of 'Writer 1') */
void buy_routine(int connfd, int id, int amount) {
	Item *temp = SearchTree(root, id);
	char *buy_msg;

	P(&(temp->w));						// mutual exclusion for the present item
	if (temp->left_stock < amount)		// only one writer can access at one time
		buy_msg = buy_error_msg;
	else {
		temp->left_stock -= amount;		// update the left_stock
		buy_msg = buy_success_msg;
	}
	V(&(temp->w));

	Rio_writen(connfd, buy_msg, MAXLINE);
}

/* Routine for 'sell' service (routine for 'Writer 2') */
void sell_routine(int connfd, int id, int amount) {
	Item *temp = SearchTree(root, id);

	P(&(temp->w));						// mutual exclusion for 'writer'
	temp->left_stock += amount;			// update the left_stock
	V(&(temp->w));

	Rio_writen(connfd, sell_success_msg, MAXLINE);
}

/* Routine for 'exit' service */
void exit_routine(int connfd) {
	Rio_writen(connfd, exit_msg, MAXLINE);
	// server has nothing to do with termination of client!
	// client will be terminated based on its own routine.
	//  ex) client check the message from server at every iteration,
	//      and if the message is "exit", then, terminate itself.
}

/* Routine for errorneous requests from clients */
void error_routine(int connfd) {
	Rio_writen(connfd, error_msg, MAXLINE);		// just send the 'error msg'
}

/* Read the 'stock.txt' file and construct the AVL tree */
void stock_load(void) {
	int id, left_stock, price;
	char eachLine[128];
	FILE *fp;

	if (!(fp = fopen("stock.txt", "rt"))) {
		fprintf(stderr, "The 'stock.txt' file does not exist.\n");
		exit(0);
	}

	while (Fgets(eachLine, sizeof(eachLine), fp)) {
		sscanf(eachLine, "%d %d %d", &id, &left_stock, &price);
		root = InsertTree(root, id, left_stock, price);
	}

	Fclose(fp);
}

/* Store the updated 'stock.txt' file (when the server terminates) */
void stock_store(void) {
	FILE *fp;

	if (!(fp = fopen("stock.txt", "wt"))) {
		fprintf(stderr, "File open error occurs in Store Routine.\n");
		exit(0);
	}

	for (int i = 0; i < print_size; i++) {		// just traverse the pointer array
		char eachLine[128];

		sprintf(eachLine, "%d %d %d\n", print[i]->ID, print[i]->left_stock, print[i]->price);

		Fputs(eachLine, fp);
	}

	Fclose(fp);
}

/* Thread routine (routine of 'Worker/Consumer Threads') */
void *thread(void *vargp) {
	Pthread_detach(pthread_self());				// reserve the reaping of thread

	while (1) {
		int n, connfd = sbuf_remove(&sbuf); 	// consume the item from the buffer
		char buf[MAXLINE];
		rio_t rio;

		Rio_readinitb(&rio, connfd);
		while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {			// get requests
			printf("server received %d bytes\n", n);
			service(connfd, buf, n);									// and service!
		}

		Close(connfd);
	}
}

/* Signal handler for SIGINT signal */
void sigint_handler(int sig) {
	int olderrno = errno;

	stock_store();				// if Ctrl+C pressed, update the 'stock.txt',
	sbuf_deinit(&sbuf);			// clear the shared buffer,
	ClearTree(root);			// clear the AVL tree.
	printf("\nServer has terminated with 'stock.txt' update!\n");
	exit(0);

	errno = olderrno;
}
/** Subroutines for Service of Stock Server End **/
/**   Subroutines for Service of Stock Server   **/



/** Subroutines for 'Producer-Consumer Problem' **/
/* Create 'Empty, Bounded, Shared FIFO buffer' which can contain n slots */
void sbuf_init(sbuf_t *sp, int n) {
	sp->buf = Calloc(n, sizeof(int));		// dynamic allocation for buffer
	sp->n = n; 								// maximum of n slots
	sp->front = sp->rear = 0; 				// initialize as empty
	Sem_init(&sp->mutex, 0, 1); 			// binary semaphore for locking
	Sem_init(&sp->slots, 0, n); 			// counting semaphore with 'n'
	Sem_init(&sp->items, 0, 0); 			// initial state for 'items' is zero
}

/* Function for clearing the shared buffer */
void sbuf_deinit(sbuf_t *sp) {
	Free(sp->buf);								// just free the pointer
}

/* Insert new item into the 'rear' point of shared buffer */
void sbuf_insert(sbuf_t *sp, int item) {
	P(&sp->slots); 								// waits for available slots
	P(&sp->mutex); 								// lock
	sp->buf[(++sp->rear) % (sp->n)] = item;		// item insertion (produce)
	V(&sp->mutex); 								// unlock
	V(&sp->items); 							// notify that there's new available item!
}

/* Delete the item at the 'front' point of shared buffer, and return it */
int sbuf_remove(sbuf_t *sp) {
	int item;

	P(&sp->items); 								// waits for available items
	P(&sp->mutex); 								// provides serialization 
	item = sp->buf[(++sp->front) % (sp->n)]; 	// item removement (consume)
	V(&sp->mutex);
	V(&sp->slots); 							// notify that there's new available slot!

	return item;
}
/*Subroutines for 'Producer-Consumer Problem' End*/



/***        Subroutines for the AVL Tree       ***/
/* Node insertion routine of AVL tree */
Item* InsertTree(Item* node, int id, int left_stock, int price) {
	Item* new_item;

	if (node == NULL) {								// if recursion met NULL,
		new_item = (Item*)malloc(sizeof(Item));		// create new node!
		new_item->ID = id;
		new_item->left_stock = left_stock;
		new_item->price = price;
		new_item->height = 0;
		new_item->left = new_item->right = NULL;	// initialization routine
		new_item->readcnt = 0;
		Sem_init(&(new_item->mutex), 0, 1);			// initialize semaphore variables
		Sem_init(&(new_item->w), 0, 1);
		print[print_size++] = new_item;				// insert into pointer array too!
		return new_item;
	}

	if (id > node->ID) {
		node->right = InsertTree(node->right, id, left_stock, price);

		if ((GetHeight(node->right) - GetHeight(node->left)) == 2) {
			if (id > node->right->ID)
				node = SingleRotateRight(node);
			else
				node = DoubleRotateRight(node);
		}											// insertion algorithm of AVL tree
	}
	else if (id < node->ID) {
		node->left = InsertTree(node->left, id, left_stock, price);

		if ((GetHeight(node->left) - GetHeight(node->right)) == 2) {
			if (id < node->left->ID)
				node = SingleRotateLeft(node);
			else
				node = DoubleRotateLeft(node);
		}
	}
	else unix_error("Stock list in stock.txt has something wrong!\n");

	node->height = GetGreater(GetHeight(node->left), GetHeight(node->right)) + 1;
	// coordinates heights!
	return node;
}

/* Inorder traversal for searching some items */
Item* SearchTree(Item* node, int id) {
	if (node == NULL || node->ID == id)
		return node;

	if (node->ID > id)
		return SearchTree(node->left, id);

	return SearchTree(node->right, id);
}

/* Inorder traversal for freeing AVL tree */
void ClearTree(Item* node) {
	if (node != NULL) {
		ClearTree(node->left);
		ClearTree(node->right);
		Free(node);
	}
}

/* Left single rotation */
Item* SingleRotateLeft(Item *nodeB) {
	Item* nodeA = NULL;

	nodeA = nodeB->left;
	nodeB->left = nodeA->right;		// rotating process
	nodeA->right = nodeB;

	nodeB->height = GetGreater(GetHeight(nodeB->left), GetHeight(nodeB->right)) + 1;
	nodeA->height = GetGreater(GetHeight(nodeA->left), GetHeight(nodeB)) + 1;

	return nodeA;
}

/* Right single rotation */
Item* SingleRotateRight(Item *nodeA) {
	Item* nodeB = NULL;

	nodeB = nodeA->right;
	nodeA->right = nodeB->left;		// rotating process
	nodeB->left = nodeA;

	nodeA->height = GetGreater(GetHeight(nodeA->left), GetHeight(nodeA->right)) + 1;
	nodeB->height = GetGreater(GetHeight(nodeB->right), GetHeight(nodeA)) + 1;

	return nodeB;
}

/* Left double rotation */
Item* DoubleRotateLeft(Item *node) {
	node->left = SingleRotateRight(node->left);

	return SingleRotateLeft(node);
}

/* Right double rotation */
Item* DoubleRotateRight(Item *node) {
	node->right = SingleRotateLeft(node->right);

	return SingleRotateRight(node);
}

/* Return the height of input node */
int GetHeight(Item *node) {
	if (node == NULL)
		return -1;					// -1 for comparison and addition
	return node->height;
}

/* Return greater one between two heights */
int GetGreater(int heightA, int heightB) {
	return (heightA > heightB) ? heightA : heightB;
}
/***      Subroutines for the AVL Tree End     ***/
/************** End of the Program ***************/