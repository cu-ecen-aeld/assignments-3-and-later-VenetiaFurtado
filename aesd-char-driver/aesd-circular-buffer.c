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
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

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
                                                                          size_t char_offset, size_t *entry_offset_byte_rtn)
{
    size_t num_iterations = 0;
    uint8_t posn = buffer->out_offs;
    size_t cumulative = 0;
    size_t i = 0;

    // input sanity check
    if (buffer == NULL || entry_offset_byte_rtn == NULL)
    {
        return NULL;
    }

    // buffer empty check
    if ((buffer->full == false) && (buffer->in_offs == buffer->out_offs))
    {
        return NULL;
    }

    // determine how many iterations we need to search for the offset

    // if buffer is full, we need to check the entire cb
    if (buffer->full == true)
    {
        num_iterations = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
    else
    {
        // iterate from oldest(out_offs) to newest(in_offs)
        if (buffer->in_offs > buffer->out_offs)
        {
            num_iterations = buffer->in_offs - buffer->out_offs;
        }
        // iterate from oldest(out_offs) to newest(in_offs) with wraparound
        else
        {
            num_iterations = (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - buffer->out_offs) + buffer->in_offs;
        }
    }

    // iterate from oldest entry (out_offs) to newest entry (in_offs)
    // and wraparound if we reach the end of the cb before reaching in_offs
    posn = buffer->out_offs;
    for (i = 0; i < num_iterations; i++)
    {
        size_t j = 0;
        for (j = 0; j < buffer->entry[posn].size; j++)
        {
            if (cumulative == char_offset)
            {
                *entry_offset_byte_rtn = j;
                return &(buffer->entry[posn]);
            }
            cumulative++;
        }

        // wraparound
        posn = (posn + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    *entry_offset_byte_rtn = 0;
    return NULL;
}

/**
 * Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
 * If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
 * new start location.
 * Any necessary locking must be handled by the caller
 * Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
 */
const char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    const char *buffptr_to_free = NULL;

    // input sanity checks
    if (buffer == NULL || add_entry == NULL)
    {
        return NULL;
    }

    // since buffer is full , increment out_offs by 1
    if (buffer->full == true)
    {
        buffptr_to_free = buffer->entry[buffer->out_offs].buffptr;
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // add new entry to cb
    buffer->entry[buffer->in_offs] = *add_entry;

    // increment in_offs and wrap around if > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // set buffer full to true if in_offs has wrapped around
    if (buffer->in_offs == buffer->out_offs)
    {
        buffer->full = true;
    }

    return buffptr_to_free;
}

/**
 * Initializes the circular buffer described by @param buffer to an empty struct
 */
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));
    buffer->full = false;
    buffer->in_offs = 0;
    buffer->out_offs = 0;
}
