#ifndef _SCENARIO_H_
#define _SCENARIO_H_

#define R_10(s)					\
  s; s; s; s; s; s; s; s; s; s;			

#define R_2(s)					\
  s; s; 
#define R_4(s)					\
  R_2(s); R_2(s);
#define R_8(s)					\
  R_4(s); R_4(s);
#define R_16(s)					\
  R_8(s); R_8(s);
#define R_32(s)					\
  R_16(s); R_16(s);
#define R_64(s)					\
  R_32(s); R_32(s);
#define R_128(s)				\
  R_64(s); R_64(s);
#define R_256(s)				\
  R_128(s); R_128(s);
#define R_512(s)				\
  R_256(s); R_256(s);

#define SCENARIO_REPEAT_UNROLL_2(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      R_2(s);								\
    };
#define SCENARIO_REPEAT_UNROLL_4(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      R_4(s);								\
    };
#define SCENARIO_REPEAT_UNROLL_8(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      R_8(s);								\
    };
#define SCENARIO_REPEAT_UNROLL_16(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      R_16(s);								\
    };
#define SCENARIO_REPEAT_UNROLL_32(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      R_32(s);								\
    };
#define SCENARIO_REPEAT_UNROLL_64(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      R_64(s);								\
    };
#define SCENARIO_REPEAT_UNROLL_128(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      R_128(s);								\
    };
#define SCENARIO_REPEAT_UNROLL_256(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      R_256(s);								\
    };
#define SCENARIO_REPEAT_UNROLL_512(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      R_512(s);								\
    };

#define R_100(s)				\
  s; s; s; s; s; s; s; s; s; s;			\
  s; s; s; s; s; s; s; s; s; s;			\
  s; s; s; s; s; s; s; s; s; s;			\
  s; s; s; s; s; s; s; s; s; s;			\
  s; s; s; s; s; s; s; s; s; s;			\
  s; s; s; s; s; s; s; s; s; s;			\
  s; s; s; s; s; s; s; s; s; s;			\
  s; s; s; s; s; s; s; s; s; s;			\
  s; s; s; s; s; s; s; s; s; s;			\
  s; s; s; s; s; s; s; s; s; s;

#define R_500(s)				\
  R_100(s); R_100(s); R_100(s); R_100(s); R_100(s);

#define SCENARIO_PRINT(s)  if (d->id == 0) { printf("#Scenario: " #s "\n"); }
#define SCENARIO_EXECT(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); s
#define SCENARIO_REPEAT(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); while (1) { s };
#define SCENARIO_REPEAT_WHILE(s,w)  SCENARIO_PRINT(s); barrier_cross(d->barrier); do { s } while (w);
#define SCENARIO_REPEAT_UNROLL_100(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      R_100(s);								\
    };

#define SCENARIO_REPEAT_UNROLL_500(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
     R_500(s);								\
    };

#define SCENARIO_REPEAT_UNROLL_1000(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      R_500(s);  R_500(s);						\
    };

#define SCENARIO_REPEAT_UNROLL_10(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      s; s; s; s; s; s; s; s; s; s;					\
    };

#define SCENARIO_REPEAT_UNROLL_50(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      R_10(s);  R_10(s);  R_10(s);  R_10(s);  R_10(s);			\
    };

#define SCENARIO_REPEAT_UNROLL_1(s)  SCENARIO_PRINT(s); barrier_cross(d->barrier); \
  while (1)								\
    {									\
      s;								\
    };

#define SCENARIO_RUN(s)				\
  SCENARIO_REPEAT_UNROLL_1(s)

#define RAX_STORE_1()				\
  asm volatile ("mov $1, %rax")
#define RBX_STORE_1()				\
  asm volatile ("mov $1, %rbx")
#define RCX_STORE_1()				\
  asm volatile ("mov $1, %rcx")
#define R9_STORE_1()				\
  asm volatile ("mov $1, %r9")
#define R10_STORE_1()				\
  asm volatile ("mov $1, %r10")
#define R11_STORE_1()				\
  asm volatile ("mov $1, %r11")
#define R12_STORE_1()				\
  asm volatile ("mov $1, %r12")
#define RN_STORE_1(n)				\
  asm volatile ("mov $1, %r" # n)
#define NOP()					\
  asm volatile ("nop")

#endif
