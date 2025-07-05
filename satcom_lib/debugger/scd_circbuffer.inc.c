/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#include "scd_common.h"


//----------------------------------------------------------------------
// Circular buffer stuff.
// 
//----------------------------------------------------------------------
/*--- Saturn-side functions. ---*/
/**
 * Init Circular buffer.
**/
void scd_cbinit(scd_data_t* d)
{
    /* Reset Circular buffer pointers. */
    d->readptr  = 0;
    d->writeptr = d->readptr;

    /* Set internals. */
    d->start_address = (unsigned long)d->rbuffer;
    d->end_address   = d->start_address + sizeof(d->rbuffer) - 1;
    d->buffer_size   = sizeof(d->rbuffer);
}


/**
 * Write type+datalen+data in Circular buffer.
 * It modifies Circular buffer's write pointer.
**/
void scd_cbwrite(scd_data_t* d, unsigned char* data, unsigned long datalen)
{
    if(d->writeptr >= d->readptr)
    {
        /* Enough free space ? */
        if((datalen + 1) > (d->buffer_size - d->writeptr + d->readptr - 1)) return;

        unsigned long end_space = d->buffer_size - d->writeptr;

        if(end_space >= datalen)
        { /* Enough space until the end of the buffer. */
            memcpy((void*)(d->start_address+d->writeptr), 
                   (void*)(data), 
                   datalen);
        }
        else
        { /* Need to write the data in 2 times: end of buffer, and the remaining from the buffer start */
            memcpy((void*)(d->start_address+d->writeptr), 
                   (void*)(data), 
                   end_space);

            memcpy((void*)(d->start_address), 
                   (void*)(data+end_space), 
                   datalen - end_space);
        }
    }
    else
    {
        /* Enough free space ? */
        if((datalen + 1) > (d->readptr - d->writeptr - 1)) return;

        memcpy((void*)(d->start_address+d->writeptr), 
               (void*)(data), 
               datalen);
    }

    /* Update write pointer. */
    unsigned long ptr = d->writeptr + datalen;
    d->writeptr = (ptr >= d->buffer_size ? ptr - d->buffer_size : ptr);
}

/**
 * Return Circular buffer's memory usage.
 * Return value is between 0(buffer empty) and 128 (buffer full).
**/
unsigned char scd_cbmemuse(scd_data_t* d)
{
    unsigned long ret;

    if(d->writeptr == d->readptr)
    { /* Simplest (and most common case). */
        ret = 0;
    }
    else if(d->writeptr >= d->readptr)
    {
        ret = (d->buffer_size - d->writeptr + d->readptr - 1) << 7;
        if(d->buffer_size != 0) ret = ret/d->buffer_size;
    }
    else //!if(d->writeptr >= d->readptr)
    {
        ret = (d->readptr - d->writeptr - 1) << 7;
        if(d->buffer_size != 0) ret = ret/d->buffer_size;
    }

    return (unsigned char)(128 - ret);
}


/*
 * Read function cannot be used as-is, because we need to receive
 * data from Saturn, and not simply memcpy it.
 * Hence, the circular buffer read function is directly implemented 
 * in SatLink -> ProcessThread.cpp::ReadCircularBufferData function.
 */
#if 0
/*--- PC-side functions. ---*/
/**
 * Read all the data in the Circular buffer.
 * Note: need at most SCD_CircularBUFF_SIZE bytes in output buffer.
 * It modifies Circular buffer's read pointer.
**/
unsigned long scd_cbread(scd_data_t* d, unsigned char* data)
{
    unsigned long read_size=0;

    /* Circular buffer is empty. */
    if(d->writeptr == d->readptr) return read_size;

    if(d->writeptr > d->readptr)
    {
        read_size = d->writeptr - d->readptr;
        memcpy((void*)(data), 
               (void*)(d->start_address+d->readptr), 
               read_size);
    }
    else
    {
        read_size = d->buffer_size - d->readptr;
        memcpy((void*)(data), 
               (void*)(d->start_address+d->readptr), 
               read_size);

        memcpy((void*)(data+read_size), 
               (void*)(d->start_address), 
               d->writeptr);
        read_size += d->writeptr;
    }

    /* Update read pointer. */
    d->readptr = d->writeptr;

    return read_size;
}
#endif



