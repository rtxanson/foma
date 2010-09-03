/*     Foma: a finite-state toolkit and library.                             */
/*     Copyright © 2008-2010 Mans Hulden                                     */

/*     This file is part of foma.                                            */

/*     Foma is free software: you can redistribute it and/or modify          */
/*     it under the terms of the GNU General Public License version 2 as     */
/*     published by the Free Software Foundation. */

/*     Foma is distributed in the hope that it will be useful,               */
/*     but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/*     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/*     GNU General Public License for more details.                          */

/*     You should have received a copy of the GNU General Public License     */
/*     along with foma.  If not, see <http://www.gnu.org/licenses/>.         */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "foma.h"
#include "zlib.h"

#define TYPE_TRANSITION 1
#define TYPE_SYMBOL 2
#define TYPE_FINAL 3
#define TYPE_PROPERTY 4
#define TYPE_END 5
#define TYPE_ERROR 6

#define READ_BUF_SIZE 4096

struct binaryline {
    int type;
    int state;
    int in;
    int target;
    int out;
    int symbol;
    char *name;
    char *value;
};

extern char *g_att_epsilon;

void io_free();
static int io_gets(char *target);
static size_t io_get_gz_file_size(char *filename);
static size_t io_get_file_size(char *filename);
static size_t io_get_regular_file_size(char *filename);
size_t io_gz_file_to_mem (char *filename);
int foma_net_print(struct fsm *net, gzFile *outfile);
struct fsm *io_net_read(char **net_name);
static inline int explode_line (char *buf, int *values);

static char *io_buf = NULL, *io_buf_ptr = NULL;

int write_prolog (struct fsm *net, char *filename) {
  struct fsm_state *stateptr;
  int i, *finals, *used_symbols, maxsigma;
  FILE *out;
  char *outstring, *instring, identifier[100];
  
  if (filename == NULL) {
    out = stdout;
  } else {
    if ((out = fopen(filename, "w")) == NULL) {
      printf("Error writing to file '%s'. Using stdout.\n", filename);
      out = stdout;
    }
    printf("Writing prolog to file '%s'.\n", filename);
  }
  fsm_count(net);
  maxsigma = sigma_max(net->sigma);
  used_symbols = xxcalloc(maxsigma+1,sizeof(int));
  finals = xxmalloc(sizeof(int)*(net->statecount));
  stateptr = net->states;
  identifier[0] = '\0';

  strcpy(identifier, net->name);

  /* Print identifier */
  fprintf(out, "%s%s%s", "network(",identifier,").\n");

  for (i=0; (stateptr+i)->state_no != -1; i++) {
    if ((stateptr+i)->final_state == 1) {
      *(finals+((stateptr+i)->state_no)) = 1;
    } else {
      *(finals+((stateptr+i)->state_no)) = 0;
    }
    if ((stateptr+i)->in != -1) {
      *(used_symbols+((stateptr+i)->in)) = 1;
    }
    if ((stateptr+i)->out != -1) {
      *(used_symbols+((stateptr+i)->out)) = 1;
    }

  }

  for (i = 3; i <= maxsigma; i++) {
    if (*(used_symbols+i) == 0) {
      instring = sigma_string(i, net->sigma);
      if (strcmp(instring,"0") == 0) instring = "%0";      
      fprintf(out, "symbol(%s, \"%s\").\n", identifier, instring);
    }
  }
  
  for (; stateptr->state_no != -1; stateptr++) {
    if (stateptr->target == -1)
      continue;
    fprintf(out, "arc(%s, %i, %i, ", identifier, stateptr->state_no, stateptr->target);
    if      (stateptr->in == 0) instring = "0";
    else if (stateptr->in == 1) instring = "?";
    else if (stateptr->in == 2) instring = "?";
    else instring = sigma_string(stateptr->in, net->sigma);
    if      (stateptr->out == 0) outstring = "0";
    else if (stateptr->out == 1) outstring = "?";
    else if (stateptr->out == 2) outstring = "?";
    else outstring = sigma_string(stateptr->out, net->sigma);

    if (strcmp(instring,"0") == 0 && stateptr->in != 0) instring = "%0";
    if (strcmp(outstring,"0") == 0 && stateptr->out != 0) outstring = "%0";
    
    if (net->arity == 2 && stateptr->in == IDENTITY && stateptr->out == IDENTITY) {
      fprintf(out, "\"?\").\n");    
    }
    else if (net->arity == 2 && stateptr->in == stateptr->out && stateptr->in != UNKNOWN) {
      fprintf(out, "\"%s\").\n", instring);
    }
    else if (net->arity == 2) 
      fprintf(out, "\"%s\":\"%s\").\n", instring, outstring);
    else if (net->arity == 1)
      fprintf(out, "\"%s\").\n", instring);
  }
  for (i = 0; i < net->statecount; i++) {
    if (*(finals+i)) {
      fprintf(out, "final(%s, %i).\n", identifier, i);
    }
  }
  if (filename != NULL) {
      fclose(out);
  }
  xxfree(finals);
  xxfree(used_symbols);
  return 1;
}

struct fsm *read_att(char *filename) {

    struct fsm_construct_handle *h;
    struct fsm *net;
    int i;
    char inword[1024], delimiters[] = "\t", *tokens[5];
    FILE *INFILE;

    INFILE = fopen(filename, "r");
    if (INFILE == NULL) {
        return(NULL);
    }

    h = fsm_construct_init(filename);
    while (fgets(inword, 1024, INFILE) != NULL) {
        if (inword[strlen(inword)-1] == '\n') {
            inword[strlen(inword)-1] = '\0';
        }
        tokens[0] = strtok(inword, delimiters);
        i = 0;
        if (tokens[0] != NULL) {
            i = 1;
            for ( ; ; ) {
                tokens[i] = strtok(NULL, delimiters);
                if (tokens[i] == NULL) {
                    break;
                }
                i++;
                if (i == 6)
                    break;
            }
        }
        if (i == 0) { continue; }
        if (i >= 4) {
            if (strcmp(tokens[2],g_att_epsilon) == 0)
                tokens[2] = "@_EPSILON_SYMBOL_@";
            if (strcmp(tokens[3],g_att_epsilon) == 0)
                tokens[3] = "@_EPSILON_SYMBOL_@";

            fsm_construct_add_arc(h, atoi(tokens[0]), atoi(tokens[1]), tokens[2], tokens[3]);
        }
        else if (i <= 3 && i > 0) {
            fsm_construct_set_final(h,atoi(tokens[0]));
        }
    }
    fsm_construct_set_initial(h,0);
    fclose(INFILE);
    net = fsm_construct_done(h);
    fsm_count(net);
    net = fsm_topsort(net);
    return(net);
}

struct fsm *fsm_read_prolog (char *filename) {
    char buf [1024], temp [1024], in [128], out[128], *temp_ptr, *temp_ptr2;
    int arity, source, target, has_net;
    struct fsm *outnet;
    struct fsm_construct_handle *outh;
    FILE *prolog_file;
    
    has_net = 0;
    prolog_file = fopen(filename, "r");
    if (prolog_file == NULL) {
	return NULL;
    }

    while (fgets(buf, 1023, prolog_file) != NULL) {
	if (strstr(buf, "network(") == buf) {
	    /* Extract network name */
	    if (has_net == 1) {
		perror("WARNING: prolog file contains multiple nets. Only returning the first one.\n");
		break;
	    } else {
		has_net = 1;
	    }
	    temp_ptr = strstr(buf, "network(")+8;
	    temp_ptr2 = strstr(buf, ").");
	    strncpy(temp, temp_ptr, (temp_ptr2 - temp_ptr));
	    temp[(temp_ptr2-temp_ptr)] = '\0';
	    
	    /* Start network */
	    outh = fsm_construct_init(temp);
	}
	if (strstr(buf, "final(") == buf) {
	    temp_ptr = strstr(buf, " ");
	    temp_ptr++;
	    temp_ptr2 = strstr(temp_ptr, ").");
	    strncpy(temp, temp_ptr, (temp_ptr2 - temp_ptr));
	    temp[(temp_ptr2-temp_ptr)] = '\0';
	    
	    fsm_construct_set_final(outh, atoi(temp));
	}
	if (strstr(buf, "symbol(") == buf) {
	    temp_ptr = strstr(buf, ", \"")+3;
	    temp_ptr2 = strstr(temp_ptr, "\").");
	    strncpy(temp, temp_ptr, (temp_ptr2 - temp_ptr));
	    temp[(temp_ptr2-temp_ptr)] = '\0';
	    if (strcmp(temp, "%0") == 0)
		strcpy(temp, "0");
	    //printf("special: %s\n",temp);
	    
	    if (fsm_construct_check_symbol(outh, temp) == -1) {
		fsm_construct_add_symbol(outh, temp);
	    }      
	    continue;
	}
	if (strstr(buf, "arc(") == buf) {
	    in[0] = '\0';
	    out[0] = '\0';
	    
	    if (strstr(buf, "\":\"") == NULL || strstr(buf, ", \":\").") != NULL) {
		arity = 1;
	    } else {
		arity = 2;
	    }
	    
	    /* Get source */
	    temp_ptr = strstr(buf, " ");
	    temp_ptr++;
	    temp_ptr2 = strstr(temp_ptr, ",");
	    strncpy(temp, temp_ptr, (temp_ptr2 - temp_ptr));
	    temp[(temp_ptr2-temp_ptr)] = '\0';
	    source = atoi(temp);
	    
	    /* Get target */
	    temp_ptr = strstr(temp_ptr2, " ");
	    temp_ptr++;
	    temp_ptr2 = strstr(temp_ptr, ",");
	    strncpy(temp, temp_ptr, (temp_ptr2 - temp_ptr));
	    temp[(temp_ptr2-temp_ptr)] = '\0';
	    target = atoi(temp);
	    
	    temp_ptr = strstr(temp_ptr2, "\"");
	    temp_ptr++;
	    if (arity == 2)  { 
		temp_ptr2 = strstr(temp_ptr, "\":");
	    } else {
		temp_ptr2 = strstr(temp_ptr, "\").");
	    }
	    strncpy(in, temp_ptr, (temp_ptr2 - temp_ptr));
	    in[(temp_ptr2 - temp_ptr)] = '\0';
	    
	    if (arity == 2) {
		temp_ptr = strstr(temp_ptr2, ":\"");
		temp_ptr += 2;
		temp_ptr2 = strstr(temp_ptr, "\").");
		strncpy(out, temp_ptr, (temp_ptr2 - temp_ptr));
		out[(temp_ptr2 - temp_ptr)] = '\0';
	    }
	    if (arity == 1 && (strcmp(in, "?") == 0)) {
		strcpy(in,"@_IDENTITY_SYMBOL_@");
	    }
	    if (arity == 2 && (strcmp(in, "?") == 0)) {
		strcpy(in,"@_UNKNOWN_SYMBOL_@");
	    }
	    if (arity == 2 && (strcmp(out, "?") == 0)) {
		strcpy(out,"@_UNKNOWN_SYMBOL_@");
	    }
	    if (strcmp(in, "0") == 0) {
		strcpy(in,"@_EPSILON_SYMBOL_@");
	    }
	    if (strcmp(out, "0") == 0) {
		strcpy(out,"@_EPSILON_SYMBOL_@");
	    }
	    if (strcmp(in, "%0") == 0) {
		strcpy(in,"0");
	    }
	    if (strcmp(out, "%0") == 0) {
		strcpy(out,"0");
	    }
	    
	    if (arity == 1) { 
		fsm_construct_add_arc(outh, source, target, in, in);	    
	    } else {
		fsm_construct_add_arc(outh, source, target, in, out);
	    }
	}
    }
    fclose(prolog_file);
    if (has_net == 1) {
	fsm_construct_set_initial(outh, 0);
	outnet = fsm_construct_done(outh);
	fsm_topsort(outnet);
	return(outnet);
    } else {
	return(NULL);
    }
}

void io_free() {
    if (io_buf != NULL) {
        xxfree(io_buf);
        io_buf = NULL;
    }
}

struct fsm *fsm_read_binary_file(char *filename) {
    char *net_name;
    struct fsm *net;

    if (io_gz_file_to_mem(filename) == 0) {
        return NULL;
    }
    net = io_net_read(&net_name);
    io_free();
    return(net);
}

int save_defined(char *filename) {
    gzFile *outfile;
    struct defined *def;
    def = get_defines();
    if (def == NULL) {
        printf("No defined networks.\n");
        return(0);
    }
    if ((outfile = gzopen(filename, "wb")) == NULL) {
        printf("Error opening file %s for writing.\n", filename);
        return(-1);
    }
    printf("Writing definitions to file %s.\n", filename);
    for ( ; def != NULL; def = def->next) {
        strcpy(def->net->name, def->name);
        foma_net_print(def->net, outfile);
    }
    gzclose(outfile);
    return(1);
}

int load_defined(char *filename) {
    struct fsm *net;
    char *net_name;
    printf("Loading definitions from %s.\n",filename);
    if (io_gz_file_to_mem(filename) == 0) {
        printf("File error.\n");
        return 0;
    }

    while ((net = io_net_read(&net_name)) != NULL) {
        add_defined(net, net_name);
    }
    io_free();
    return(1);
}

static inline int explode_line (char *buf, int *values) {

    int i, j, items;
    j = i = items = 0;
    for (;;) {
        for (i = j; *(buf+j) != ' ' && *(buf+j) != '\0'; j++) { }
        if (*(buf+j) == '\0') {
            *(values+items) = atoi(buf+i);
            items++;
            break;
        } else{
            *(buf+j) = '\0';
            *(values+items) = atoi(buf+i);
            items++;
            j++;
            i = j;
        }
    }
    return(items);
}

/* The file format we use is an extremely simple text format */
/* which is gzip compressed through libz and consists of the following sections: */

/* ##foma-net VERSION##*/
/* ##props## */
/* PROPERTIES LINE */
/* ##sigma## */
/* ...SIGMA LINES... */
/* ##states## */
/* ...TRANSITION LINES... */ 
/* ##end## */

/* Several networks may be concatenated in one file */

/* The initial identifier is "##foma-net 1.0##" */
/* where 1.0 is the version number for the file format */
/* followed by the line "##props##" */
/* which is followed by a line of space separated integers */
/* which correpond to: */

/* arity arccount statecount linecount finalcount pathcount is_deterministic */
/* is_pruned is_minimized is_epsilon_free is_loop_free is_completed name  */

/* where name is used if defined networks are saved/loaded */

/* the section beginning with "##sigma##" consists of lines with two fields: */
/* number string */
/* correponding to the symbol number and the symbol string */

/* the section beginning with "##states##" consists of lines of ASCII integers */
/* with 2-5 fields to avoid some redundancy in every line corresponding to a */
/* transition where otherwise state numbers would be unnecessarily repeated and */
/* out symbols also (if in = out as is the case for recognizers/simple automata) */

/* The information depending on the number of fields in the lines is as follows: */

/* 2: in target (here state_no is the same as the last mentioned one and out = in) */
/* 3: in out target (again, state_no is the same as the last mentioned one) */
/* 4: state_no in target final_state (where out = in) */
/* 5: state_no in out target final_state */

/* There is no harm in always using 5 fields; however this will take up more space */

/* As in struct fsm_state, states without transitions are represented as a 4-field: */
/* state_no -1 -1 final_state (since in=out for 4-field lines, out = -1 as well) */

/* AS gzopen will read uncompressed files as well, one can gunzip a file */
/* that contains a network and still read it */

struct fsm *io_net_read(char **net_name) {

    char buf[READ_BUF_SIZE];
    struct fsm *net;
    struct fsm_state *fsm;
    
    char *new_symbol;
    int i, items, new_symbol_number, laststate, lineint[5], *cm;
    char last_final;

    if (io_gets(buf) == 0) {
        return NULL;
    }
    
    net = fsm_create("");

    if (strcmp(buf, "##foma-net 1.0##") != 0) {
        printf("File format error foma!\n");
        return NULL;
    }
    io_gets(buf);
    if (strcmp(buf, "##props##") != 0) {
        printf("File format error props!\n");
        return NULL;
    }
    /* Properties */
    io_gets(buf);
    sscanf(buf, "%i %i %i %i %i %lld %i %i %i %i %i %i %s", &net->arity, &net->arccount, &net->statecount, &net->linecount, &net->finalcount, &net->pathcount, &net->is_deterministic, &net->is_pruned, &net->is_minimized, &net->is_epsilon_free, &net->is_loop_free, &net->is_completed, buf);
    strcpy(net->name, buf);
    *net_name = strdup(buf);
    io_gets(buf);

    /* Sigma */
    if (strcmp(buf, "##sigma##") != 0) {
        printf("File format error sigma!\n");
        return NULL;
    }
    net->sigma = sigma_create();
    for (;;) {
        io_gets(buf);
        if (buf[0] == '#') break;
        new_symbol = strstr(buf, " ");
        new_symbol[0] = '\0';
        new_symbol++;
        sscanf(buf,"%i", &new_symbol_number);
        sigma_add_number(net->sigma, new_symbol, new_symbol_number);
    }
    /* States */
    if (strcmp(buf, "##states##") != 0) {
        printf("File format error!\n");
        return NULL;
    }
    net->states = xxmalloc(net->linecount*sizeof(struct fsm_state));
    fsm = net->states;
    laststate = -1;
    for (i=0; ;i++) {
        io_gets(buf);
        if (buf[0] == '#') break;

        /* scanf is just too slow here */

        //items = sscanf(buf, "%i %i %i %i %i",&lineint[0], &lineint[1], &lineint[2], &lineint[3], &lineint[4]);

        items = explode_line(buf, &lineint[0]);

        switch (items) {
        case 2:
            (fsm+i)->state_no = laststate;
            (fsm+i)->in = lineint[0];
            (fsm+i)->out = lineint[0];
            (fsm+i)->target = lineint[1];
            (fsm+i)->final_state = last_final;
            break;
        case 3:
            (fsm+i)->state_no = laststate;
            (fsm+i)->in = lineint[0];
            (fsm+i)->out = lineint[1];
            (fsm+i)->target = lineint[2];
            (fsm+i)->final_state = last_final;
            break;
        case 4:
            (fsm+i)->state_no = lineint[0];
            (fsm+i)->in = lineint[1];
            (fsm+i)->out = lineint[1];
            (fsm+i)->target = lineint[2];
            (fsm+i)->final_state = lineint[3];
            laststate = lineint[0];
            last_final = lineint[3];
            break;
        case 5:
            (fsm+i)->state_no = lineint[0];
            (fsm+i)->in = lineint[1];
            (fsm+i)->out = lineint[2];
            (fsm+i)->target = lineint[3];
            (fsm+i)->final_state = lineint[4];
            laststate = lineint[0];
            last_final = lineint[4];
            break;
        default:
            printf("File format error\n");
            return NULL;
        }
        if (laststate > 0) {
            (fsm+i)->start_state = 0;
        } else if (laststate == -1) {
            (fsm+i)->start_state = -1;
        } else {
            (fsm+i)->start_state = 1;
        }

    }
    if (strcmp(buf, "##cmatrix##") == 0) {
        cmatrix_init(net);
        cm = net->medlookup->confusion_matrix;
        for (;;) {
            io_gets(buf);
            if (buf[0] == '#') break;
            sscanf(buf,"%i", &i);
            *cm = i;
            cm++;
        }
    }
    if (strcmp(buf, "##end##") != 0) {
        printf("File format error!\n");
        return NULL;
    }
    return(net);
}

static int io_gets(char *target) {
    int i;
    for (i = 0; *(io_buf_ptr+i) != '\n' && *(io_buf_ptr+i) != '\0'; i++) {
        *(target+i) = *(io_buf_ptr+i);
    }   
    *(target+i) = '\0';
    if (*(io_buf_ptr+i) == '\0')
        io_buf_ptr = io_buf_ptr + i;
    else
        io_buf_ptr = io_buf_ptr + i + 1;

    return(i);
}

int foma_net_print(struct fsm *net, gzFile *outfile) {
    struct sigma *sigma;
    struct fsm_state *fsm;
    int i, maxsigma, laststate, *cm;

    /* Header */
    gzprintf(outfile, "%s","##foma-net 1.0##\n");

    /* Properties */
    gzprintf(outfile, "%s","##props##\n");
    gzprintf(outfile, "%i %i %i %i %i %lld %i %i %i %i %i %i %s\n",net->arity, net->arccount, net->statecount, net->linecount, net->finalcount, net->pathcount, net->is_deterministic, net->is_pruned, net->is_minimized, net->is_epsilon_free, net->is_loop_free, net->is_completed, net->name);
    
    /* Sigma */
    gzprintf(outfile, "%s","##sigma##\n");
    for (sigma = net->sigma; sigma != NULL && sigma->number != -1; sigma = sigma->next) {
        gzprintf(outfile, "%i %s\n",sigma->number, sigma->symbol);
    }

    /* State array */
    laststate = -1;
    fsm = net->states;
    gzprintf(outfile, "%s","##states##\n");
    for (fsm = net->states; fsm->state_no !=-1; fsm++) {
        if (fsm->state_no != laststate) {
            if (fsm->in != fsm->out) {
                gzprintf(outfile, "%i %i %i %i %i\n",fsm->state_no, fsm->in, fsm->out, fsm->target, fsm->final_state);
            } else {
                gzprintf(outfile, "%i %i %i %i\n",fsm->state_no, fsm->in, fsm->target, fsm->final_state);
            }
        } else {
            if (fsm->in != fsm->out) {
                gzprintf(outfile, "%i %i %i\n", fsm->in, fsm->out, fsm->target);
            } else {
                gzprintf(outfile, "%i %i\n", fsm->in, fsm->target);
            }
        }
        laststate = fsm->state_no;
    }
    /* Sentinel for states */
    gzprintf(outfile, "-1 -1 -1 -1 -1\n");

    /* Store confusion matrix */
    if (net->medlookup != NULL && net->medlookup->confusion_matrix != NULL) {

        gzprintf(outfile, "%s","##cmatrix##\n");
        cm = net->medlookup->confusion_matrix;
        maxsigma = sigma_max(net->sigma)+1;
        for (i=0; i < maxsigma*maxsigma; i++) {
            gzprintf(outfile, "%i\n", *(cm+i));
        }
    }

    /* End */
    gzprintf(outfile, "%s","##end##\n");
    return(1);
}

int net_print_att(struct fsm *net, FILE *outfile) {
    struct fsm_state *fsm;
    struct fsm_sigma_list *sl;
    int i, prev;

    fsm = net->states;
    sl = sigma_to_list(net->sigma);
    if (sigma_max(net->sigma) >= 0) {
        (sl+0)->symbol = g_att_epsilon;
    }
    for (i=0; (fsm+i)->state_no != -1; i++) {
        if ((fsm+i)->target != -1) {
            fprintf(outfile, "%i\t%i\t%s\t%s\n",(fsm+i)->state_no,(fsm+i)->target, (sl+(fsm+i)->in)->symbol, (sl+(fsm+i)->out)->symbol);            
        }
    }
    prev = -1;
    for (i=0; (fsm+i)->state_no != -1; prev = (fsm+i)->state_no, i++) {
        if ((fsm+i)->state_no != prev && (fsm+i)->final_state == 1) {
            fprintf(outfile, "%i\n",(fsm+i)->state_no);
        }
    }
    xxfree(sl);
    return(1);
}

static size_t io_get_gz_file_size(char *filename) {

    FILE    *infile;
    size_t    numbytes;
    unsigned char bytes[4];
    unsigned int ints[4], i;

    /* The last four bytes in a .gz file shows the size of the uncompressed data */
    infile = fopen(filename, "r");
    fseek(infile, -4, SEEK_END);
    fread(&bytes, 1, 4, infile);
    fclose(infile);
    for (i = 0 ; i < 4 ; i++) {
        ints[i] = bytes[i];
    }
    numbytes = ints[0] | (ints[1] << 8) | (ints[2] << 16 ) | (ints[3] << 24);
    return(numbytes);
}

static size_t io_get_regular_file_size(char *filename) {

    FILE    *infile;
    size_t    numbytes;

    infile = fopen(filename, "r");
    fseek(infile, 0L, SEEK_END);
    numbytes = ftell(infile);
    fclose(infile);
    return(numbytes);
}


static size_t io_get_file_size(char *filename) {
    gzFile *FILE;
    size_t size;
    FILE = gzopen(filename, "r");
    if (FILE == NULL) {
        return(0);
    }
    if (gzdirect(FILE) == 1) {
        gzclose(FILE);
        size = io_get_regular_file_size(filename);
    } else {
        gzclose(FILE);
        size = io_get_gz_file_size(filename);
    }
    return(size);
}

size_t io_gz_file_to_mem(char *filename) {

    size_t size;
    gzFile *FILE;

    size = io_get_file_size(filename);
    if (size == 0) {
        return 0;
    }
    io_buf = xxmalloc((size+1)*sizeof(char));
    FILE = gzopen(filename, "rb");
    gzread(FILE, io_buf, size);
    gzclose(FILE);
    *(io_buf+size) = '\0';
    io_buf_ptr = io_buf;
    return(size);
}

char *file_to_mem(char *name) {

    FILE    *infile;
    size_t    numbytes;
    char *buffer;
    infile = fopen(name, "r");
    if(infile == NULL) {
        printf("Error opening file '%s'\n",name);
        return NULL;
    }
    fseek(infile, 0L, SEEK_END);
    numbytes = ftell(infile);
    fseek(infile, 0L, SEEK_SET);
    buffer = (char*)xxmalloc_atomic((numbytes+1) * sizeof(char));
    if(buffer == NULL) {
        printf("Error reading file '%s'\n",name);
        return NULL;
    }
    if (fread(buffer, sizeof(char), numbytes, infile) != numbytes) {
        printf("Error reading file '%s'\n",name);
        return NULL;
    }
    fclose(infile);
    *(buffer+numbytes)='\0';
    return(buffer);
}