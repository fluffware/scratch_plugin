#ifndef __JSON_PARSE_H__OXHNK0486U__
#define __JSON_PARSE_H__OXHNK0486U__

#include <stdint.h>

#define JSON_NO_TYPE 0
#define JSON_INTEGER 1
#define JSON_BOOLEAN 2
#define JSON_STRING 3
#define JSON_ARRAY 4
#define JSON_OBJECT 5
#define JSON_NULL 6

struct JSONValue
{
  int type;
  union {
    long integer;
    int boolean;
    const uint8_t *string;
    const uint8_t *array;
    const uint8_t *object;
  } value;
};

struct JSONStringBuffer
{
  uint8_t *data;
  unsigned int capacity;
  unsigned int length;
};

int
json_parse_string(const uint8_t **pp, 
		  int (*callback)(const uint8_t *block, unsigned int len, 
				  void *cb_data), 
		  void *cb_data);

int
json_parse_int(const uint8_t **pp, long *value);
int
json_parse_value(const uint8_t **pp, struct JSONValue *value);

int
json_skip_string(const uint8_t **pp);

void
json_skip_white(const uint8_t **pp);

int 
json_skip_comma(const uint8_t **pp);

int
json_iterate_array(const uint8_t **pp, 
		   int (*callback)(const uint8_t **pp, void *cb_data), 
		   void *cb_data);

int
json_iterate_object(const uint8_t **pp,
		    char *key, unsigned int key_capacity, 
		    int (*callback)(const uint8_t **pp, const char *key, 
				    void *cb_data), 
		    void *cb_data);

int
json_string_callback(const uint8_t *block, unsigned int len, void *cb_data);

int
json_parse_string_buffer(const uint8_t **pp, 
			 uint8_t *buffer, unsigned int len);

#endif /* __JSON_PARSE_H__OXHNK0486U__ */
