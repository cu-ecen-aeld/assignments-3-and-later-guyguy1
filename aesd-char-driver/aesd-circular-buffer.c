/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>
#else
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#endif

#include "aesd-circular-buffer.h"

#define BUFFER_SIZE AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED


static uint8_t get_number_of_entries(struct aesd_circular_buffer *buffer)
{
    if (buffer->in_offs > buffer->out_offs)
    {
        return buffer->in_offs - buffer->out_offs;
    }
    else if (buffer->in_offs < buffer->out_offs)
    {
        return (BUFFER_SIZE - buffer->out_offs + buffer->in_offs);
    }
    else
    {
        return buffer->full ? BUFFER_SIZE : 0;
    }
}

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    uint8_t num_entries = get_number_of_entries(buffer);

    for (int i = 0; i < num_entries; ++i)
    {
        uint8_t entry_index = (buffer->out_offs + i) % BUFFER_SIZE;
        struct aesd_buffer_entry *entry = &buffer->entry[entry_index];

        if (entry->size > char_offset)
        {
            *entry_offset_byte_rtn = char_offset;
            return entry;
        }

        char_offset -= entry->size;
    }
    
    return NULL;
}

size_t aesd_circular_buffer_get_num_bytes(struct aesd_circular_buffer* buffer)
{
    uint8_t num_entries = get_number_of_entries(buffer);
    size_t result = 0;

    for (int i = 0; i < num_entries; ++i)
    {
        uint8_t entry_index = (buffer->out_offs + i) % BUFFER_SIZE;
        struct aesd_buffer_entry *entry = &buffer->entry[entry_index];

        result += entry->size;
    }

    return result;
}

long aesd_circular_buffer_calculate_offset(struct aesd_circular_buffer *buffer, 
                                           uint32_t entry_index,
                                           uint32_t offset_in_entry)
{
    uint8_t num_entries = get_number_of_entries(buffer);
    long result = 0;

    if (entry_index >= num_entries)
    {
        return -EINVAL;
    }

    for (int i = 0; i <= entry_index; ++i)
    {
        uint8_t current_entry_index = (buffer->out_offs + i) % BUFFER_SIZE;
        struct aesd_buffer_entry *entry = &buffer->entry[current_entry_index];

        if (i == entry_index)
        {
            if (offset_in_entry >= entry->size)
            {
                return -EINVAL;
            }

            result += offset_in_entry;
            return result;
        }

        result += entry->size;
    }

    printk("ERROR: This place should be impossible to reach");
    return -1;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    if (buffer->full && buffer->in_offs != buffer->out_offs)
    {
        printk("ERROR: buffer is marked full but in_offs = %d, out_offs = %d\n", buffer->in_offs, buffer->out_offs);
        return;
    }

    if (buffer->full)
    {
        kfree(buffer->entry[buffer->in_offs].buffptr);
    }

    buffer->entry[buffer->in_offs].buffptr = add_entry->buffptr;
    buffer->entry[buffer->in_offs].size = add_entry->size;

    buffer->in_offs++;

    // Check wrap around
    if (buffer->in_offs > BUFFER_SIZE)
    {
        printk("ERROR: buffer->in_offs is too big (%d)\n", buffer->in_offs);
        return;
    }
    else if (buffer->in_offs == BUFFER_SIZE)
    {
        buffer->in_offs = 0;
    }

    // handle buffer->full
    if (buffer->full)
    {
        buffer->out_offs = buffer->in_offs;
    }
    else if (buffer->in_offs == buffer->out_offs)
    {
        buffer->full = true;
    }
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
