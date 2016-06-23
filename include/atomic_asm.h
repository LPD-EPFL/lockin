#ifndef _ATOMIC_ASM_H_
#define _ATOMIC_ASM_H_

static inline uint64_t
cas_u64(volatile uint64_t* p, uint64_t o, uint64_t n)
{
#define lock "lock; "
/* #define lock "" */
  uint64_t __ret;
  asm volatile(lock "cmpxchgq %2,%1"
	       : "=a" (__ret), "+m" (*p)
	       : "r" (n), "0" (o)
	       : "memory");
  return __ret;
}

static inline uint8_t
cas_u8(volatile uint8_t* p, uint8_t o, uint8_t n)
{
#define lock "lock; "
/* #define lock "" */
  uint8_t __ret;
  asm volatile(lock "cmpxchgb %2,%1"
	       : "=a" (__ret), "+m" (*p)
	       : "r" (n), "0" (o)
	       : "memory");
  return __ret;
}


#endif	/* _ATOMIC_ASM_H_ */
