#ifndef FIXED_POINT_H
#define FIXED_POINT_H

typedef int fixed_t;
#define P 17
#define Q 14
#define F (1 << Q)
#define INT_TO_FIXED(n) ((n) * F)
#define FIXED_TO_INT(x) ((x) / F)
#define FIXED_TO_INT_ROUND(x) (((x) >= 0) ? ((x) + F / 2) / F : ((x) - F / 2) / F)
#define FIXED_TO_INT_TRUNC(x) ((x) / F)
#define ADD_FIXED(x, y) ((x) + (y))
#define SUB_FIXED(x, y) ((x) - (y))
#define ADD_INT(x, n) ((x) + INT_TO_FIXED(n))
#define SUB_INT(x, n) ((x) - INT_TO_FIXED(n))
#define MUL_FIXED(x, y) (((int64_t) (x)) * (y) / F)
#define MUL_INT(x, n) ((x) * (n))
#define DIV_FIXED(x, y) (((int64_t) (x)) * F / (y))
#define DIV_INT(x, n) ((x) / (n))



#endif