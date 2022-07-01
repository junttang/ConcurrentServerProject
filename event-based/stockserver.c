/**************************************************
 * Title: SP-Project 2   -  Event-Based StockServer
 * Summary: 'Event-Based Concurrent Stock Server'
 for studying the concepts of network programming,
 I/O multiplexing, fine-grained programming, pros
 and cons of event-based concurrency, etc
 *  |Date              |Author             |Version
	|2022-05-21        |Park Junhyeok      |1.0.0
**************************************************/

/****************** Declaration ******************/
/* Headers */
#include "csapp.h"


/* Preprocessor Directives */
#define MAX_ITEM	1000000			/* size of pointer array */


/* Types */
typedef struct item {				/* node structure of AVL tree */
	int ID;							// ID, left_stock, price : attributes of stock item
	int left_stock;
	int price;
	int height;						// balance factor of node (for AVL operations)
	struct item *right;				// left and right link of node
	struct item *left;
}Item;

typedef struct {					/* structure for I/O Multiplexing */
	int maxfd;
	fd_set read_set;				// bit vector for 'Active Descriptors'
	fd_set ready_set;				// subset of 'read_set'
	int nready;						// num of file descriptors that has pending inputs
	int maxi;
	int clientfd[FD_SETSIZE];
	rio_t clientrio[FD_SETSIZE];
} Pool;

typedef enum {						/* enumeration for choosing the type of service */
	_show_, _buy_, _sell_, _exit_, _error_
}command;


/* Global Variables */
Item *root = NULL;					/* root of AVL tree */
Item *print[MAX_ITEM];				/* pointer array for 'show' routine, etc */
int print_size;						/* size of pointer array */

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


/* Subroutines for I/O Multiplexing */
void init_pool(int listenfd, Pool *p);
void add_client(int connfd, Pool *p);
void check_client(Pool *p);


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
void sigint_handler(int sig);


/**************** Implementation *****************/
/**   Subroutines for Service of Stock Server   **/
/* Main routine of 'Event-Based Concurrent Stock Server' */
int main(int argc, char **argv) {
	int listenfd, connfd;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	char client_hostname[MAXLINE], client_port[MAXLINE];
	static Pool pool;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}
	stock_load();							// load the 'stock.txt', and construct tree
	Signal(SIGINT, sigint_handler);			// install the SIGINT handler

	listenfd = Open_listenfd(argv[1]);
	init_pool(listenfd, &pool);				// initialize the pool for I/O Multiplexing

	while (1) {
		pool.ready_set = pool.read_set;
		pool.nready = Select(pool.maxfd + 1, &pool.ready_set, NULL, NULL, NULL);

		if (FD_ISSET(listenfd, &pool.ready_set)) {			// if pending at listenfd,
			clientlen = sizeof(struct sockaddr_storage);
			connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

			Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
			printf("Connected to (%s, %s)\n", client_hostname, client_port);

			add_client(connfd, &pool);						// add new connfd to pool
		}

		check_client(&pool);				// check if there's any pendings at connfds
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

/* Routine for 'show' service */
void show_routine(int connfd) {
	char printbuf[MAXLINE] = "";

	for (int i = 0; i < print_size; i++) {
		char s1[128], s2[128], s3[128];

		rio_itoa(print[i]->ID, s1, 10);
		rio_itoa(print[i]->left_stock, s2, 10); 	// transform integer into string
		rio_itoa(print[i]->price, s3, 10);

		strcat(printbuf, s1); strcat(printbuf, " ");
		strcat(printbuf, s2); strcat(printbuf, " ");
		strcat(printbuf, s3); strcat(printbuf, "\n");
	}
	Rio_writen(connfd, printbuf, MAXLINE);
}

/* Routine for 'buy' service */
void buy_routine(int connfd, int id, int amount) {
	Item *temp = SearchTree(root, id);
	char *buy_msg;

	if (temp->left_stock < amount)
		buy_msg = buy_error_msg;
	else {
		temp->left_stock -= amount;		// update the left_stock
		buy_msg = buy_success_msg;
	}

	Rio_writen(connfd, buy_msg, MAXLINE);
}

/* Routine for 'sell' service */
void sell_routine(int connfd, int id, int amount) {
	Item *temp = SearchTree(root, id);

	temp->left_stock += amount;			// update the left_stock

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

/* Signal handler for SIGINT signal */
void sigint_handler(int sig) {
	int olderrno = errno;

	stock_store();				// if Ctrl+C pressed, update the 'stock.txt',
	ClearTree(root);			// clear the AVL tree.
	printf("\nServer has terminated with 'stock.txt' update!\n");
	exit(0);

	errno = olderrno;
}
/** Subroutines for Service of Stock Server End **/
/**   Subroutines for Service of Stock Server   **/


/***     Subroutines for I/O Multiplexing      ***/
/* Initialization routine for the pool structure */
void init_pool(int listenfd, Pool *p) {
	p->maxi = -1;
	for (int i = 0; i < FD_SETSIZE; i++)
		p->clientfd[i] = -1;				// initialize clientfds as -1

	p->maxfd = listenfd;
	FD_ZERO(&p->read_set);
	FD_SET(listenfd, &p->read_set);			// set 'listenfd' in read_set
}

/* Add new connected descriptors into the pool */
void add_client(int connfd, Pool *p) {
	int i;
	p->nready--;							// decrement the available slots

	for (i = 0; i < FD_SETSIZE; i++) {
		if (p->clientfd[i] < 0) {
			p->clientfd[i] = connfd;					// insert into the fd array
			Rio_readinitb(&p->clientrio[i], connfd);	// ready for using RIO package

			FD_SET(connfd, &p->read_set);				// ready for checking pending

			if (connfd > p->maxfd)
				p->maxfd = connfd;
			if (i > p->maxi)							// coordination
				p->maxi = i;

			break;
		}
	}

	if (i == FD_SETSIZE)
		app_error("Error in add_client!\n");
}

/* Check if there are any pending inputs, and provide service */
void check_client(Pool *p) {
	int n, connfd;
	char buf[MAXLINE];
	rio_t rio;

	for (int i = 0; (i <= p->maxi) && (p->nready > 0); i++) {
		connfd = p->clientfd[i];
		rio = p->clientrio[i];

		if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) {	// if pending,
			if ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {		// then read!
				printf("server received %d bytes\n", n);
				service(connfd, buf, n);							// and service!
			}
			else {
				Close(connfd);
				FD_CLR(connfd, &p->read_set);
				p->clientfd[i] = -1;
			}
		}
	}
} 
/***    Subroutines for I/O Multiplexing End   ***/


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