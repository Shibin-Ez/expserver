#include "xps_pipe.h"

xps_pipe_t *xps_pipe_create(xps_core_t *core, size_t buff_thresh, xps_pipe_source_t *source,
                            xps_pipe_sink_t *sink) {
  assert(core != NULL);
  assert(buff_thresh > 0);
  assert(source != NULL);
  assert(sink != NULL);

  // Alloc memory for pipe instance
  xps_pipe_t *pipe = malloc(sizeof(xps_pipe_t));
  if (pipe == NULL) {
    logger(LOG_ERROR, "xps_pipe_create()", "malloc() failed for 'pipe'");
    return NULL;
  }

  // Init values
  pipe->core = core;
  pipe->source = NULL;
  pipe->sink = NULL;
  pipe->buff_list = xps_buffer_list_create();
  pipe->buff_thresh = buff_thresh;

  /* Add pipe to 'pipes' list of core*/
  vec_push(&core->pipes, pipe);

  /*Attach source and sink to pipe*/
  xps_pipe_attach_source(pipe, source);
  xps_pipe_attach_sink(pipe, sink);

  /*Make both source and sink of pipe active*/
  pipe->source->active = true;
  pipe->sink->active = true;

  logger(LOG_DEBUG, "xps_pipe_create()", "created pipe");

  return pipe;
}

void xps_pipe_destroy(xps_pipe_t *pipe) {
  assert(pipe != NULL);

  if (pipe->source)
    xps_pipe_detach_source(pipe);

  if (pipe->sink)
    xps_pipe_detach_sink(pipe);

  /*Set NULL in 'pipes' list of core and increment n_null_pipes*/
  for (int i = 0; i < pipe->core->pipes.length; i++) {
    if (pipe == pipe->core->pipes.data[i]) {
      pipe->core->pipes.data[i] = NULL;
      pipe->core->n_null_pipes++;
      break;
    }
  }

  /*Destroy the buff_list of pipe*/
  xps_buffer_list_destroy(pipe->buff_list);

  /*Free the pipe*/
  free(pipe);

  logger(LOG_DEBUG, "xps_pipe_destroy()", "destroyed pipe");
}

bool xps_pipe_is_readable(xps_pipe_t *pipe) { return pipe->buff_list->len > 0; }

bool xps_pipe_is_writable(xps_pipe_t *pipe) { return pipe->buff_list->len < pipe->buff_thresh; }

int xps_pipe_attach_source(xps_pipe_t *pipe, xps_pipe_source_t *source) {
  /*assert pipe and source not null*/
  assert(pipe != NULL);
  assert(source != NULL);

  /*check whether pipe already has a source and return E_FAIL*/
  if (pipe->source)
    return E_FAIL;

  pipe->source = source;
  source->pipe = pipe;

  return OK;
}

int xps_pipe_detach_source(xps_pipe_t *pipe) {
  /*assert pipe not null*/
  assert(pipe != NULL);

  /*check whether pipe has no source and return E_FAIL*/
  if (pipe->source == NULL)
    return E_FAIL;

  pipe->source->pipe = NULL;
  pipe->source = NULL;

  return OK;
}

int xps_pipe_attach_sink(xps_pipe_t *pipe, xps_pipe_sink_t *sink) {
  /*assert pipe and sink not null*/
  assert(pipe != NULL);
  assert(sink != NULL);

  /*check whether pipe already has a sink and return E_FAIL*/
  if (pipe->sink)
    return E_FAIL;

  pipe->sink = sink;
  sink->pipe = pipe;

  return OK;
}

int xps_pipe_detach_sink(xps_pipe_t *pipe) {
  /*assert pipe not null*/
  assert(pipe != NULL);

  /*check whether pipe has no sink and return E_FAIL*/
  if (pipe->sink == NULL)
    return E_FAIL;

  pipe->sink->pipe = NULL;

  pipe->sink = NULL;
  return OK;
}

xps_pipe_source_t *xps_pipe_source_create(void *ptr, xps_handler_t handler_cb,
                                          xps_handler_t close_cb) {
  /*assert ptr, handler_cb, close_cb not null*/
  assert(ptr != NULL);
  assert(handler_cb != NULL);
  assert(close_cb != NULL);

  /*Allocate memory for 'source' instance, if null returned log the error and
   * return*/
  xps_pipe_source_t *source = malloc(sizeof(xps_pipe_source_t));
  if (source == NULL) {
    logger(LOG_ERROR, "xps_pipe_source_create()", "malloc failed for source");
    return NULL;
  }

  // Init values
  source->pipe = NULL;
  source->ready = false;
  source->active = false;
  source->handler_cb = handler_cb;
  source->close_cb = close_cb;
  source->ptr = ptr;

  logger(LOG_DEBUG, "xps_pipe_source_create()", "create pipe_source");

  return source;
}

void xps_pipe_source_destroy(xps_pipe_source_t *source) {
  /*assert source not null*/
  assert(source != NULL);

  // Detach from pipe
  if (source->pipe != NULL)
    xps_pipe_detach_source(source->pipe);

  free(source);

  logger(LOG_DEBUG, "xps_pipe_source_destroy()", "destroyed pipe_source");
}

int xps_pipe_source_write(xps_pipe_source_t *source, xps_buffer_t *buff) {
  /*assert source, buff not null*/
  assert(source != NULL);
  assert(buff != NULL);

  /*Check if source not have a pipe*/
  if (source->pipe == NULL) {
    logger(LOG_ERROR, "xps_pipe_source_write()", "source is not attached to a pipe");
    return E_FAIL;
  }

  /*Check whether pipe is not writable*/
  if (!xps_pipe_is_writable(source->pipe)) {
    logger(LOG_ERROR, "xps_pipe_source_write()", "pipe is not writable");
    return E_FAIL;
  }

  // Duplicate buffer
  xps_buffer_t *dup_buff = xps_buffer_duplicate(buff);
  if (dup_buff == NULL) {
    logger(LOG_ERROR, "xps_pipe_source_write()", "xps_buffer_duplicate() failed");
    return E_FAIL;
  }

  /*Append dup_buff to buff_list of pipe*/
  xps_buffer_list_append(source->pipe->buff_list, dup_buff);

  return OK;
}

xps_pipe_sink_t *xps_pipe_sink_create(void *ptr, xps_handler_t handler_cb, xps_handler_t close_cb) {
  assert(ptr != NULL);
  assert(handler_cb != NULL);
  assert(close_cb != NULL);

  xps_pipe_sink_t *sink = malloc(sizeof(xps_pipe_sink_t));
  if (sink == NULL) {
    logger(LOG_ERROR, "xps_pipe_sink_create()", "malloc failed for sink");
    return NULL;
  }

  sink->pipe = NULL;
  sink->active = false;
  sink->ready = false;
  sink->handler_cb = handler_cb;
  sink->close_cb = close_cb;
  sink->ptr = ptr;

  logger(LOG_DEBUG, "xps_pipe_sink_create()", "create pipe_sink");

  return sink;
}

void xps_pipe_sink_destroy(xps_pipe_sink_t *sink) {
  assert(sink != NULL);

  if (sink->pipe) {
    xps_pipe_detach_sink(sink->pipe);
  }

  free(sink);

  logger(LOG_DEBUG, "xps_pipe_sink_destroy()", "destroyed pipe_sink");
}

xps_buffer_t *xps_pipe_sink_read(xps_pipe_sink_t *sink, size_t len) {
  /*assert sink not null and len greater than 0*/
  assert(sink != NULL);
  assert(len > 0);

  /*Check if sink not have a pipe*/
  if (sink->pipe == NULL) {
    logger(LOG_ERROR, "xps_pipe_sink_read()", "sink is not attached to a pipe");
    return NULL;
  }

  /*Check if requested length is not available*/
  if (len > sink->pipe->buff_list->len) {
    logger(LOG_ERROR, "xps_pipe_sink_read()", "requested length more than available");
    return NULL;
  }

  xps_buffer_t *buff = xps_buffer_list_read(sink->pipe->buff_list, len);
  if (buff == NULL) {
    logger(LOG_ERROR, "xps_pipe_sink_read()", "xps_buffer_list_read() failed");
    return NULL;
  }

  return buff;
}

int xps_pipe_sink_clear(xps_pipe_sink_t *sink, size_t len) {
  assert(sink != NULL);
  assert(len > 0);

  /*Check if sink not have a pipe*/
  if (sink->pipe == NULL) {
    logger(LOG_ERROR, "xps_pipe_sink_clear()", "sink is not attached to a pipe");
    return E_FAIL;
  }

  /*Check whether requested length not available*/
  if (len > sink->pipe->buff_list->len) {
    logger(LOG_ERROR, "xps_pipe_sink_clear()", "requested length more than available");
    return E_FAIL;
  }

  if (xps_buffer_list_clear(sink->pipe->buff_list, len) != OK) {
    logger(LOG_ERROR, "xps_pipe_sink_clear()", "xps_buffer_list_clear() failed");
    return E_FAIL;
  }

  return OK;
}