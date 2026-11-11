/***************************************************************************************************
 * @file cyclic_buf.h
 * @brief Cyclic buffer
 **************************************************************************************************/

 #ifndef _CYCLIC_BUF_H
 #define _CYCLIC_BUF_H
 
 /* > Includes *************************************************************************************/
 #include <stdint.h>
 #include <stdbool.h>
 
 /* > Defines **************************************************************************************/
 // #define RING_BUFFER_USE_CRITICAL_REGION
 
 
 /* > Type definitions *****************************************************************************/
 typedef struct
 {
   volatile uint16_t r_idx;
   volatile uint16_t w_idx;
   uint16_t size;
 
   uint8_t* buf;
 } cyclic_buf_t;
 
 
 /* > Global Function Decleration ******************************************************************/
 
 static inline void CyclicBuf_init(cyclic_buf_t *self, uint8_t* buf, uint16_t size)
 {
   self->r_idx = 0;
   self->w_idx = 0;
   self->buf = buf;
   self->size = size;
 }
 
 static inline void CyclicBuf_write(cyclic_buf_t *self, uint8_t data)
 {
   uint16_t w_idx = self->w_idx;
   self->buf[w_idx] = data;
   self->w_idx = ((uint16_t)(w_idx + 1)) % self->size;
 }
 
 static inline void CyclicBuf_writeArray(cyclic_buf_t *self, uint8_t* data, uint16_t length)
 {
   while(length--) {
     CyclicBuf_write(self, *data++);
   }
 }
 
 static inline uint8_t CyclicBuf_read(cyclic_buf_t *self)
 {
   uint16_t r_idx = self->r_idx;
   uint8_t data = self->buf[r_idx];
   self->r_idx = ((uint16_t)(r_idx + 1)) % self->size;
 
   return data;
 }
 
 static inline void CyclicBuf_readArray(cyclic_buf_t *self, uint8_t* data, uint16_t length)
 {
   while(length--) {
     *data++ = CyclicBuf_read(self);
   }
 }
 
 static inline uint8_t* CyclicBuf_getReadPtr(cyclic_buf_t *self) {
   return (&self->buf[self->r_idx]);
 }
 
 static inline uint8_t* CyclicBuf_getWritePtr(cyclic_buf_t *self) {
   return (&self->buf[self->w_idx]);
 }
 
 static inline void CyclicBuf_drop(cyclic_buf_t *self, uint16_t length) {
   self->r_idx = ((uint16_t)(self->r_idx + length)) % self->size;
 }
 
 static inline uint16_t CyclicBuf_getSize(cyclic_buf_t *self) {
   return self->size;
 }
 
 static inline uint16_t CyclicBuf_getReadable(cyclic_buf_t *self) {
   uint16_t w_idx = self->w_idx;
   uint16_t r_idx = self->r_idx;
 
   return (((uint16_t)(w_idx - r_idx)) % self->size);
 }
 
 static inline uint16_t CyclicBuf_getWritable(cyclic_buf_t *self) {
   uint16_t w_idx = self->w_idx;
   uint16_t r_idx = self->r_idx;
 
   return (((uint16_t)(r_idx - w_idx - 1)) % self->size);
 }
 
 static inline bool CyclicBuf_isEmpty(cyclic_buf_t *self) {
   return (self->w_idx == self->r_idx);
 }
 
 static inline bool CyclicBuf_isFull(cyclic_buf_t *self) {
   return (CyclicBuf_getWritable(self) == 0);
 }
 
 
 #endif /* _CYCLIC_BUF_H */