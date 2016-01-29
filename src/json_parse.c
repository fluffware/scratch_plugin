#include "json_parse.h"
#include <stdlib.h>
#include <string.h>
int
json_parse_string(const uint8_t **pp, 
		  int (*callback)(const uint8_t *block, unsigned int len, 
				  void *cb_data), 
		  void *cb_data)
{
  const uint8_t *start;
  const uint8_t *p = *pp;
  if (*p != '"') return 0;
  p++;
  while(1) {
    start = p;
    while (*p != '"' && *p != '\\' && *p != '\0') p++;
    if (!callback(start, p - start, cb_data)) return 0;
    if (*p == '\0') return 0;
    if (*p == '"') {
      *pp = p + 1;
      return 1;
    }
    p++;
    switch(*p) {
    case 'b':
      if (!callback((const uint8_t*)"\b", 1, cb_data)) return 0;
      p++;
      break;
    case 'f':
      if (callback((const uint8_t*)"\f", 1, cb_data)) return 0;
      p++;
      break;
    case 'n':
      if (callback((const uint8_t*)"\n", 1, cb_data)) return 0;
      p++;
      break;
    case 'r':
      if (!callback((const uint8_t*)"\r", 1, cb_data)) return 0;
      p++;
      break;
    case 't':
      if (!callback((const uint8_t*)"\t", 1, cb_data)) return 0;
      p++;
      break;
    default:
      if (!callback(p, 1, cb_data)) return 0;
      p++;
    }
  }
}

int
json_parse_int(const uint8_t **pp, long *value)
{
  const uint8_t *end;
  *value = strtol((const char*)*pp, (char**)&end, 10);
  if (*pp == end) return 0;
  *pp = end;
  return 1;
}

static int
match_token(const uint8_t **pp, const char *token)
{
  const uint8_t *p = *pp;
  while(*token != '\0' && *p == *token) {
    p++;
    token++;
  }
  if (*token != '\0') return 0;
  *pp = p;
  return 1;
}
int
json_parse_value(const uint8_t **pp, struct JSONValue *value)
{
  switch(**pp) {
  case '"':
    value->type = JSON_STRING;
    value->value.string = *pp;
    break;
  case '[':
    value->type = JSON_ARRAY;
    value->value.string = *pp;
    break;
  case '{':
    value->type = JSON_OBJECT;
    value->value.string = *pp;
    break;
  case 't':
    value->type = JSON_BOOLEAN;
    value->value.boolean = 1;
    return match_token(pp, "true");
  case 'f':
    value->type = JSON_BOOLEAN;
    value->value.boolean = 0;
    return match_token(pp, "false");
  case 'n':
    value->type = JSON_NULL;
    return match_token(pp, "null");
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
  case '-':
    value->type = JSON_INTEGER;
    return json_parse_int(pp, &value->value.integer);
  default:
    return 0;
  }
  return 1;
}

static int
skip_string_cb(const uint8_t *block, unsigned int len, 
			  void *cb_data)
{
  return 1;
}

int
json_skip_string(const uint8_t **pp)
{
  return json_parse_string(pp, skip_string_cb, NULL);
}

void
json_skip_white(const uint8_t **pp)
{
  const uint8_t *p = *pp;
  while(*p == ' ' || *p == '\t') p++;
  *pp = p;
}

int 
json_skip_comma(const uint8_t **pp)
{
  const uint8_t *p = *pp;
  json_skip_white(&p);
  if (*p != ',') return 0;
  p++;
  json_skip_white(&p);
  *pp = p;
  return 1;
}


/* The callback is expected to parse the value and advance the pointer */
int
json_iterate_array(const uint8_t **pp, 
		   int (*callback)(const uint8_t **pp, void *cb_data), 
		   void *cb_data)
{
  const uint8_t *p = *pp;
  if (*p != '[') return 0;
  p++;
  json_skip_white(&p);
  if (*p == ']') {
    *pp = p;
    return 1; /* Empty array */
  }
  if (!callback(&p, cb_data)) return 0;
  json_skip_white(&p);
  while(1) {
    json_skip_white(&p);
    if (*p == ']') break;
    if (*p != ',') return 0;
    p++;
    json_skip_white(&p);
    if (!callback(&p, cb_data)) return 0;
  }
      
  *pp = p;
  return 1;
}

/* The callback is expected to parse the value and advance the pointer.
   key is a buffer for storing the key string. */
int
json_iterate_object(const uint8_t **pp,
		    char *key, unsigned int key_capacity, 
		    int (*callback)(const uint8_t **pp, const char *key, 
				    void *cb_data), 
		    void *cb_data)
{
  struct JSONStringBuffer key_buf;
  const uint8_t *p = *pp;
  if (*p != '{') return 0;
  p++;
  json_skip_white(&p);
  if (*p == '}') {
    *pp = p;
    return 1; /* Empty object */
  }
  key_buf.data = (uint8_t*)key;
  key_buf.capacity = key_capacity;
  while(1) {
    json_skip_white(&p);
    key_buf.length = 0;
    json_parse_string(&p, json_string_callback, &key_buf);
    key_buf.data[key_buf.length] = '\0';
    json_skip_white(&p);
    if (*p != ':') return 0;
    p++;
    json_skip_white(&p);
    if (!callback(&p, key, cb_data)) return 0;
    json_skip_white(&p);
    if (*p == '}') break;
    if (*p != ',') return 0;
    p++;
  }
      
  *pp = p + 1;
  return 1;
}
int
json_string_callback(const uint8_t *block, unsigned int len, void *cb_data)
{
  struct JSONStringBuffer *buffer = cb_data;
  if (buffer->length + len >= buffer->capacity) return 0;
  if (len > 0) {
    memcpy(buffer->data + buffer->length, block, len);
    buffer->length += len;
  }
  return 1;
}

int
json_parse_string_buffer(const uint8_t **pp, 
			 uint8_t *buffer, unsigned int len)
{
  struct JSONStringBuffer str;
  str.data = buffer;
  str.capacity = len - 1; /* Leave room for '\0' */
  str.length = 0;
  if (!json_parse_string(pp, json_string_callback, &str)) return 0;
  buffer[str.length] = '\0';
  return 1;
}
