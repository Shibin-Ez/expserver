#include "xps_file.h"
#include <assert.h>

void file_source_handler(void *ptr);
void file_source_close_handler(void *ptr);

xps_file_t *xps_file_create(xps_core_t *core, const char *file_path,
                            int *error) {
  /*assert*/
  assert(core != NULL);
  assert(file_path != NULL);

  *error = E_FAIL;

  // Opening file
  FILE *file_struct = fopen(file_path, "rb");

  /*handle EACCES,ENOENT or any other error*/
  if (file_struct == NULL) {
    /*logs EACCES,ENOENT or any other error*/
    logger(LOG_ERROR, "xps_file_create()", "failed with code %d", errno);
    return NULL;
  }

  // Getting size of file

  // Seeking to end
  if (fseek(file_struct, 0, SEEK_END) != 0) {
    /*logs error*/
    logger(LOG_ERROR, "xps_file_create()",
           "fskeek() failed on setting SEEK_END");

    /*close file_struct*/
    fclose(file_struct);

    return NULL;
  }

  // Getting curr position which is the size
  long temp_size = ftell(file_struct);
  if (temp_size < 0) {
    /*logs error*/
    logger(LOG_ERROR, "xps_file_create()", "ftell() failed");

    /*close file_struct*/
    fclose(file_struct);

    return NULL;
  }

  // Seek back to start
  if (fseek(file_struct, 0, SEEK_SET) != 0) {
    /*logs error*/
    logger(LOG_ERROR, "xps_file_create()",
           "fskeek() failed on setting SEEK_SET");

    /*close file_struct*/
    fclose(file_struct);

    return NULL;
  }

  const char *mime_type = xps_get_mime(file_path);

  /*Alloc memory for instance of xps_file_t*/
  xps_file_t *file = malloc(sizeof(xps_file_t));

  xps_pipe_source_t *source = xps_pipe_source_create(file, file_source_handler,
                                                     file_source_close_handler);

  /*if source is null, close file_struct and return*/
  if (source == NULL) {
    fclose(file_struct);
    return NULL;
  }

  // Init values
  source->ready = true;

  /*initialise the fields of file instance*/
  file->file_path = file_path;
  file->file_struct = file_struct;
  file->core = core;
  file->mime_type = mime_type;
  file->source = source;
  file->size = temp_size;

  *error = OK;

  logger(LOG_DEBUG, "xps_file_create()", "created file");

  return file;
}

void xps_file_destroy(xps_file_t *file) {
  /*assert*/
  assert(file != NULL);

  // close the file
  fclose(file->file_struct);

  // destroy the pipe source
  xps_pipe_source_destroy(file->source);

  // free the file data structure
  free(file);

  logger(LOG_DEBUG, "xps_file_destroy()", "destroyed file");
}

void file_source_handler(void *ptr) {
  /*assert*/
  assert(ptr != NULL);

  xps_pipe_source_t *source = ptr;
  
  /*get file from source ptr*/
  xps_file_t *file = source->ptr;

  /*create buffer and handle any error*/
  xps_buffer_t *buff = xps_buffer_create(file->size, 0, NULL);

  // Read from file
  size_t read_n = fread(buff->data, 1, buff->size, file->file_struct);
  buff->len = read_n;

  // Checking for read errors
  if (ferror(file->file_struct)) {
	  /*destroy buff, file and return*/
    xps_buffer_destroy(buff);
    xps_file_destroy(file);
    return;
  }

  // If end of file reached
  if (read_n == 0 && feof(file->file_struct)) {
    /*destroy buff, file and return*/
    xps_buffer_destroy(buff);
    xps_file_destroy(file);
    return;
  }

  /*Write to pipe form buff*/
  xps_pipe_source_write(source, buff);

	/*destroy buff*/
  xps_buffer_destroy(buff);
}

void file_source_close_handler(void *ptr) {
  /*assert*/
  assert(ptr != NULL);

	xps_pipe_source_t *source = ptr;
  
  /*get file from source ptr*/
	xps_file_t *file = source->ptr;

  /*destroy file*/
  xps_file_destroy(file);
}