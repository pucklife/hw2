#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "errors.h"

typedef struct dict {
  char *word;
  int count;
  struct dict *next;
} dict_t;

char *
make_word( char *word ) {
  return strcpy( malloc( strlen( word )+1 ), word );
}



dict_t *
make_dict(char *word) {
  dict_t *nd = (dict_t *) malloc( sizeof(dict_t) );
  nd->word = make_word( word );
  nd->count = 1;
  nd->next = NULL;
  return nd;
}

dict_t *
insert_word( dict_t *d, char *word ) {
  
  //   Insert word into dict or increment count if already there
  //   return pointer to the updated dict
  
  dict_t *nd;
  dict_t *pd = NULL;		// prior to insertion point 
  dict_t *di = d;		// following insertion point
  // Search down list to find if present or point of insertion
  while(di && ( strcmp(word, di->word ) >= 0) ) { 
    if( strcmp( word, di->word ) == 0 ) { 
      di->count++;		// increment count 
      return d;			// return head 
    }
    pd = di;			// advance ptr pair
    di = di->next;
  }
  nd = make_dict(word);		// not found, make entry 
  nd->next = di;		// entry bigger than word or tail 
  if (pd) {
    pd->next = nd;
    return d;			// insert beond head 
  }
  return nd;
}

void print_dict(dict_t *d) {
  while (d) {
    printf("[%d] %s\n", d->count, d->word);
    d = d->next;
  }
}

int
get_word( char *buf, int n, char *line, char **position) {
  int inword = 0;
  char c;  
  char *ptr = *position;
  while( (c = *ptr) != NULL ) {
    //printf("The %dst char is %c\n",inword,c);
    if (!isalpha(c)) {
      buf[inword] = '\0';	// terminate the word string
      //printf("Going to return now\n");
      *ptr++;
      *position = ptr;
      return 1;
    } 
    if (isalpha(c)) {
      buf[inword++] = c;
      *ptr++;
      //printf("added another letter   ");
    }
  }
  //buf[inword] = '\0';
  //printf("the buffer is: %s\n",buf);
  return 0;			// no more words
}


#define MAXLINE 1000
char *
readline( FILE *rfile ) {
  /* Read a line from a file and return a new line string object */
  char buf[MAXLINE];
  int len;
  char *result = NULL;
  char *line = fgets( buf, MAXLINE, rfile );
  if( line ) {
    len = strnlen( buf, MAXLINE );
    result = strncpy( malloc( len+1 ), buf , len+1 );
  }
  return result;
}




typedef struct sharedobject {
  dict_t *dictionary;   // pointer to dictionary
  bool flag;     // to coordinate between consumers: when the flag is true it means the dictionary is available to be written to
  pthread_mutex_t flaglock;  // mutex for 'flag'
  pthread_cond_t flag_true;  // conditional variable for 'flag == true'
} so_t;

typedef struct targ {
  FILE *textfile;      // text file to be read
  so_t *soptr;   // pointer to shared object
} targ_t;


bool waittilltrue( so_t *so );




#define MAXWORD 1024
void *
consumer( void *arg ) {
  //printf("\nThread started\n");
  int status;
  targ_t *targ = (targ_t *) arg;
  FILE *txt = targ->textfile;    // textfile from which the dictionary is to be compiled.
  so_t *so = targ->soptr;  // shared object
  char *line;
  
  while ( (line = readline( txt )) ) {
    waittilltrue( so ); // wait until the flag is 'true' (i.e., dictionary can be written to) and acquire the lock
    // we're holding the lock
    
    char wordbuf[MAXWORD];
    char *position = line;
    //printf("Next line to add to dictionary:%s",line);
    while(get_word( wordbuf, MAXWORD, line, &position )){;
  	so->dictionary = insert_word(so->dictionary, wordbuf);
    }
    if( (status = releasetrue( so )) != 0)  // set flag to 'true', signal 'flag_true', and release the lock
      err_abort( status, "unlock mutex" );
  }
  release_exit( so );
  int *ret = malloc( sizeof(int) ); 
  *ret = 1; 
  pthread_exit(ret);
}
  
bool
waittilltrue( so_t *so ) {
  // wait until the codition "so->flag == true" is met
  int status;
  if( (status = pthread_mutex_lock( &so->flaglock  )) != 0 ) // lock the object to get access to the flag
    err_abort( status, "lock mutex" );
  while( so->flag != true ) { // check the predicate associated with 'so->flag_true'
    // realease the lock and wait (done atomically)
    pthread_cond_wait( &so->flag_true, &so->flaglock ); // return locks the mutex
  }
  // we're holding the lock AND so->flag == val
  return true;
} 
  

int
releasetrue( so_t *so ) {
  so->flag = true;
  pthread_cond_signal( &so->flag_true );
  return pthread_mutex_unlock( &so->flaglock );
}

int
release_exit( so_t *so ) {
  so->flag = true;
  pthread_cond_signal( &so->flag_true );
  return pthread_mutex_unlock( &so->flaglock );
}



#define NUM_CONSUMERS 4
int
main( int argc, char *argv[] ) {
  dict_t *d = NULL;
  FILE *infile = stdin;
  void *ret;
  if (argc >= 2) {
    infile = fopen (argv[1],"r");
  }
  if( !infile ) {
    printf("Unable to open %s\n",argv[1]);
    exit( EXIT_FAILURE );
  }
  
  so_t *share = malloc(sizeof(so_t));
  share->dictionary=d;
  share->flag = true; // initially, dictionary is ready to be written to.
  
  targ_t *carg = malloc(sizeof(targ_t));
  carg->textfile = infile;
  carg->soptr = share;
  
  int status;
  pthread_t cons[NUM_CONSUMERS];  // consumer threads
  if( (status = pthread_mutex_init( &share->flaglock, NULL )) != 0 )
    err_abort( status, "mutex init" );
  if( (status = pthread_cond_init( &share->flag_true, NULL )) != 0 )
    err_abort( status, "flag_false init" );
    


  for( int i = 0; i < NUM_CONSUMERS; ++i ) {
    if( (status =  pthread_create( &cons[i], NULL, consumer, carg) ) != 0 )
      err_abort( status, "create consumer thread" );
  }
  
  for (int i = 0; i < NUM_CONSUMERS; ++i) {
    if( (status = pthread_join( cons[i],&ret )) != 0){
    	err_abort( status, "join consumer thread" );
    }
    printf( "main: consumer number %d joined\n", i );
  }
  //printf("final********************************\n\n\n");
  print_dict( share->dictionary );
  

  if( (status = pthread_mutex_destroy( &share->flaglock )) != 0)
    err_abort( status, "destroy mutex" );
  if( (status = pthread_cond_destroy( &share->flag_true )) != 0)
    err_abort( status, "destroy flag_true" );
  
  pthread_exit(NULL);
  free( share );  // destroy shared object
  free( carg );
  fclose( infile );
  

  
}



