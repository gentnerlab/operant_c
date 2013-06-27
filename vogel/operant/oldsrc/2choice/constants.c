/*
 *  FILE: constants.c
 *  AUTH: mcc
 *  DESC: 
 *  DATE: Tue Feb 19 13:18:00 2002
 *  $Id$
 */


#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

typedef enum { CONSTS_TYPE_int = 1, CONSTS_TYPE_double, CONSTS_TYPE_string } consts_type;

typedef struct {
	consts_type type;
	
	int int_value;
	double double_value;
	char *string_value;
} consts_value;

typedef struct _consts_list {
	char *key;
	consts_value *value;
	struct _consts_list *next;
} consts_list;

/* big fat global. */
consts_list *consts__ = NULL;

void consts_insert(consts_list **consts__, char *curr_key, consts_value *curr_value) {
    consts_list *curr_list;

	if (NULL == *consts__) {
	  *consts__ = malloc(sizeof(consts_list));
	  memset(*consts__, 0, sizeof(consts_list));
	  (*consts__)->key = curr_key;
	  (*consts__)->value = curr_value;
	} else {
	  consts_list *list = malloc(sizeof(consts_list));
	  memset(list, 0, sizeof(consts_list));
	  list->key = curr_key;
	  list->value = curr_value;
	  list->next = NULL;

	  curr_list = *consts__;

	  while (NULL != curr_list->next) {
		curr_list = curr_list->next;
	  }
	
	  curr_list->next = list;
	}
	/* the end */
}

consts_value *consts_lookup(consts_list *consts__, char *name) {
	consts_list *curr_list = consts__;

	while (NULL != curr_list) {
		if (0 == strcmp(name, curr_list->key)) {
		  return curr_list->value;
		} else {
		  curr_list = curr_list->next;
		}
	}

	return NULL;
}

int consts_int(char *name) {
	consts_value *lookup;
	
	if (NULL == (lookup = (consts_value*) consts_lookup(consts__, name))) {
		/* that didn't work. */
		fprintf(stderr, "consts: cannot find integer constant %s\n", name);
		exit(1);
	} else if (CONSTS_TYPE_int != lookup->type) {
		fprintf(stderr, "consts: constant %s is not of type integer\n", name);
		exit(1);
	}

	return lookup->int_value;
}

double consts_double(char *name) {
	consts_value *lookup;
	
	if (NULL == (lookup = (consts_value*) consts_lookup(consts__, name))) {
		/* that didn't work. */
		fprintf(stderr, "consts: cannot find double constant %s\n", name);
		exit(1);
	} else if (CONSTS_TYPE_double != lookup->type) {
		fprintf(stderr, "consts: constant %s is not of type double\n", name);
		exit(1);
	}

	return lookup->double_value;
}

char *consts_string(char *name) {
	consts_value *lookup;
	
	if (NULL == (lookup = (consts_value*) consts_lookup(consts__, name))) {
		/* that didn't work. */
		fprintf(stderr, "consts: cannot find string constant %s\n", name);
		exit(1);
	} else if (CONSTS_TYPE_string != lookup->type) {
		fprintf(stderr, "consts: constant %s is not of type string\n", name);
		exit(1);
	}

	return lookup->string_value;
}

static char *parse_line(char **file_mmap_ptr) {
	char *retval = NULL;
	int i=0;
	
	while ('\n' != (*file_mmap_ptr)[i]) {
		i++;
	}

	retval = calloc(i + 1, sizeof(char));
	strncpy(retval, (*file_mmap_ptr), i);

	// skip the '\n'
	*file_mmap_ptr += i + 1;
	
	return retval;
}

static int parse_int(char **file_mmap_ptr, int *i_out) {
	char *endptr = NULL;
	
	*i_out = (int) strtol(*file_mmap_ptr, &endptr, 10);

	if (endptr == *file_mmap_ptr) {
		/* it didn't work */
		return 0;
	} else if ('.' == endptr[0]) {
		/* this is actually a double -- bail! */
		return 0;
	} else {
		*file_mmap_ptr += (endptr - *file_mmap_ptr);
		return 1;
	}
}

static int parse_double(char **file_mmap_ptr, double *d_out) {
	char *endptr = NULL;
	
	*d_out = (double) strtod(*file_mmap_ptr, &endptr);

	if (endptr == *file_mmap_ptr) {
		/* it didn't work */
		return 0;
	} else {
		*file_mmap_ptr += (endptr - *file_mmap_ptr);
		return 1;
	}
}



static void parse_spaces(char **file_mmap_ptr) {
	int i=0; 

	while ((' ' == (*file_mmap_ptr)[i]) ||
		   ('\t' == (*file_mmap_ptr)[i])) {
		(*file_mmap_ptr)++;
	}
}

static char *parse_string(char **file_mmap_ptr) {
	char *retval = calloc(1024, sizeof(char));
	int i=0;

	parse_spaces(file_mmap_ptr);
	
	while ((' ' != (*file_mmap_ptr)[i]) &&
		   ('\n' != (*file_mmap_ptr)[i]) &&
		   ('\0' != (*file_mmap_ptr)[i])) {
		retval[i] = (*file_mmap_ptr)[i];
		i++;
	}

	*file_mmap_ptr += i;

	return retval;
}


static int num_lines (char *file_mmap, int filesize) {
	int retval = 0;
	int i=0;

	for (i=0; i< filesize; i++) {
		if ('\n' == file_mmap[i]) {
			retval++;
		}
	}
	return retval;
}

/* looks for file "name.const" */
void consts_init(char *name) {
	int filesize = 0;
	char *filename = calloc(1024, sizeof(char));
	struct stat statbuf;
	int fd = 0;

	void *file_mmap = NULL;
	char *curr_line = NULL;
	char *curr_key = NULL;
	int int_value = 0;
	double double_value = 0.0;
	char *string_value = NULL;

	consts_value *curr_value;

	int n_lines = 0;
	int i=0;
	
	strcpy(filename, name);
	strcat(filename, ".const");
	
	if (stat (filename, &statbuf) < 0) {
		fprintf(stderr, "consts_init(): cannot stat %s\n.", filename);
		return;
	}

	filesize = statbuf.st_size;
	fd = open (filename, O_RDONLY);

	if (-1 == fd) {
		fprintf(stderr, "consts_init(): cannot open %s.\n", filename);
		return;
	}
	
	file_mmap = mmap (NULL, filesize, PROT_READ, MAP_PRIVATE, fd, 0);

	consts__ = NULL;
	
	/* read lines ... */
	n_lines = num_lines(file_mmap, filesize);
	
	for (i=0; i < n_lines; i++) {
		/* read up to a '\n' */
		curr_line = parse_line((char**) &file_mmap);
		/*printf("got line: %s\n", curr_line);*/

		if (0 == strlen(curr_line)) {
			continue;
		}

		if ('#' == curr_line[0]) {
			continue;
		}
		
		parse_spaces((char**) &curr_line);

		curr_key = parse_string(&curr_line);

		parse_spaces(&curr_line);

		curr_value = malloc(sizeof(consts_value));
		memset(curr_value, 0, sizeof(consts_value));

		/* try to parse an int with strnol */
		if (parse_int (&curr_line, &int_value)) {
			/**curr_value = ((consts_value) { CONSTS_TYPE_int, int_value , 0.0, NULL });*/
			curr_value->type = CONSTS_TYPE_int;
			curr_value->int_value = int_value;
			curr_value->double_value = 0.0;
			curr_value->string_value = NULL;
		} else if (parse_double (&curr_line, &double_value)) {
			/**curr_value = ((consts_value) { CONSTS_TYPE_double, 0, double_value, (char*) NULL });*/
			curr_value->type = CONSTS_TYPE_double;
			curr_value->int_value = 0;
			curr_value->double_value = double_value;
			curr_value->string_value = NULL;
		} else {
			string_value = parse_string (&curr_line);
			/**curr_value = ((consts_value) { CONSTS_TYPE_string, 0, 0.0, string_value });*/
			curr_value->type = CONSTS_TYPE_string;
			curr_value->int_value = 0;
			curr_value->double_value = 0.0;
			curr_value->string_value = string_value;
		}

		/* add curr_value to the hash */
		consts_insert(&consts__, curr_key, curr_value);
	}
	
}


