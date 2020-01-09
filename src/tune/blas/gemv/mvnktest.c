/*
 *             Automatically Tuned Linear Algebra Software v3.10.1
 * Copyright (C) 2010 R. Clint Whaley
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions, and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. The name of the ATLAS group or the names of its contributers may
 *      not be used to endorse or promote products derived from this
 *      software without specific written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE ATLAS GROUP OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "atlas_misc.h"
#include "atlas_tst.h"
#include "atlas_lvl2.h"
#include "atlas_level1.h"
#include <ctype.h>
int FAx=0, MAx=0, FAy=0, MAy=0, FAa=0, MAa=0;
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

struct FA_allocs
{
   void *mem, *memA;
   struct FA_allocs *next;
} *allocQ=NULL;

struct FA_allocs *NewAlloc(size_t size, struct FA_allocs *next,
                           int align, int misalign)
/*
 * Allocates size allocation that is aligned to [align], but not aligned
 * to [misalign].  Therefore, misalign > align.  Align must minimally be sizeof
 * while misalign may be 0 if we don't need to avoid a particular alignment.
 */
{
   void *vp;
   char *cp;
   struct FA_allocs *ap;
   int n, i;
   const int malign = align >= misalign ? align : misalign;

   n = size + align + align + malign;
   i = (n >> 3)<<3;
   if (n != i)
      n += n - i;
   cp = malloc(n + sizeof(struct FA_allocs));
   assert(cp);
   ap = (struct FA_allocs *) (cp + n);
   ap->mem = cp;
/*
 * Align to min alignment
 */
   ap->memA = align ? (void*) ((((size_t) cp)/align)*align + align) : cp;
/*
 * Misalign to misalign
 * We often need to make sure to unaligned addresses share the same modulo
 * so that they have the *same* degree of misalignment (so that their alignment
 * can be fixed by simple peeling), and so in this case force the address
 * modulo the misalign to be the exact align value.
 */
   if (misalign)
      ap->memA = (void*)((((size_t)ap->memA)/malign)*malign + malign + align);
   ap->next = next;
   return(ap);
}

/*
 * no-align malloc free retaining system default behavior
 */
void *NA_malloc(size_t size)
{
   return(malloc(size));
}
void *NA_calloc(size_t n, size_t size)
{
   return(calloc(n, size));
}
void NA_free(void *ptr)
{
   free(ptr);
}


/*
 * malloc/free pair that aligns data to align, but not to misalign
 */
void *FA_malloc(size_t size, int align, int misalign)
{
   if ((!misalign && align <= 8) || !size)
      return(malloc(size));
   else
   {
      allocQ = NewAlloc(size, allocQ, align, misalign);
      return(allocQ->memA);
   }
}
void *FA_calloc(size_t n, size_t size, int align, int misalign)
{
   char *cp;
   int *ip;
   double *dp;
   size_t i;
   size_t tsize;
   tsize = n * size;
   cp = FA_malloc(tsize, align, misalign);
   if (size == sizeof(int))
      for (ip=(int*)cp,i=0; i < n; i++)
        ip[i] = 0;
   else if (size == sizeof(double))
      for (dp=(double*)cp,i=0; i < n; i++)
        dp[i] = 0.0;
   else
      for (i=0; i < tsize; i++)
        cp[i] = 0;
   return(cp);
}

void FA_free(void *ptr, int align, int misalign)
/*
 * Part of malloc/free pair that aligns data to FALIGN
 */
{
   struct FA_allocs *ap, *prev;
   if (ptr)
   {
      if ((!misalign && align <= 8))
         free(ptr);
      else
      {
         for (ap=allocQ; ap && ap->memA != ptr; ap = ap->next) prev = ap;
         if (!ap)
         {
            fprintf(stderr, "Couldn't find mem=%ld\nmemQ=\n", (size_t)ptr);
            for (ap=allocQ; ap; ap = ap->next)
               fprintf(stderr, "   %ld, %ld\n", (size_t)ap->memA,
                       (size_t)ap->mem);
         }
         assert(ap);
         if (ap == allocQ)
            allocQ = allocQ->next;
         else
            prev->next = ap->next;
         free(ap->mem);
      }
   }
}

static void dumb_gemv
   (int Conj, ATL_CINT M, ATL_CINT N, const SCALAR alpha,
    const TYPE *A, ATL_CINT lda, const TYPE *X, ATL_CINT incX,
    const SCALAR beta, TYPE *Y, ATL_CINT incY)
/*
 * If (conj) y = alpha*conj(A^T)*x + beta*y
 * else  y = alpha*(A^T)*x + beta*y
 * Since this is no-transpose, and A always MxN:
 *   Y is of length M
 *   X is of length N
 */
{
   ATL_INT j;
   #ifdef TCPLX
      const TYPE ra=(*alpha), ia=alpha[1];
      TYPE rx, ix, xa[2];
      const int lda2 = lda+lda, incX2 = incX+incX;
   #else
      TYPE xa;
      const int lda2 = lda, incX2 = incX;
   #endif
/*
 * Apply beta to Y
 */
   if (!SCALAR_IS_ONE(beta))
   {
      if (SCALAR_IS_ZERO(beta))
         Mjoin(PATL,zero)(M, Y, incY);
      else
         Mjoin(PATL,scal)(M, beta, Y, incY);
   }
   if (SCALAR_IS_ZERO(alpha) || N < 1)
      return;
   for (j=0; j < N; j++, A += lda2, X += incX2)
   {
      #ifdef TCPLX
         rx = *X;
         ix = X[1];
         xa[0] = rx*ra - ix*ia;
         xa[1] = rx*ia + ix*ra;
      #else
        xa = alpha * *X;
      #endif
      Mjoin(PATL,axpy)(M, xa, A, 1, Y, incY);
   }
}
static int CheckAns(int M, int N, TYPE *G, int incG, TYPE *T, int incT)
{
   int i, ierr=0;
   const int NN = M;
   const double nadds = NN, nmuls = NN+2;
   #ifdef TREAL
      const double epsmul = 2*(nadds+nmuls);
   #else
      const int incG2 = incG+incG, incT2 = incT+incT;
      const double epsmul = 2*(nadds+4*nmuls);
   #endif
   TYPE maxerr, diff, g, t;
   maxerr = epsmul * Mjoin(PATL,epsilon)();
#ifdef TREAL
   for (i=0; i < NN; i++, G += incG, T += incT)
   {
      g = *G;
      g = Mabs(g);
      t = *T;
      t = Mabs(t);
      diff = g - t;
      diff = Mabs(diff);
      if (diff > maxerr)
      {
         if (!ierr)
            ierr = i+1;
         fprintf(stderr, "Y(%d): Good=%f, Computed=%f, diff=%f\n",
                 i, *G, *T, diff);
      }
   }
#else
   for (i=0; i < NN+NN; i += 2, G += incG2, T += incT2)
   {
      g = *G;
      g = Mabs(g);
      t = *T;
      t = Mabs(t);
      diff = g - t;
      diff = Mabs(diff);
      if (diff > maxerr)
      {
         if (!ierr)
            ierr = (i>>2)+1;
         fprintf(stderr, "Yr(%d): Good=%f, Computed=%f, diff=%f\n",
                 i, *G, *T, diff);
      }
      g = G[1];
      g = Mabs(g);
      t = T[1];
      t = Mabs(t);
      diff = g - t;
      diff = Mabs(diff);
      if (diff > maxerr)
      {
         if (!ierr)
            ierr = (i>>2)+1;
         fprintf(stderr, "Yi(%d): Good=%f, Computed=%f, diff=%f\n",
                 i, G[1], T[1], diff);
      }
   }
#endif
   return(ierr);
}
#define NX N
#define NY M
void ATL_UGEMV(ATL_CINT M, ATL_CINT N, const TYPE *A, ATL_CINT lda,
               const TYPE *X, TYPE *Y);

static int RunTest(int M, int N, int lda)
{
   #ifdef TCPLX
      TYPE one[2] = {ATL_rone, ATL_rzero};
      #ifdef BETA0
         const TYPE beta[2] = {ATL_rzero, ATL_rzero};
      #else
         const TYPE beta[2] = {ATL_rone, ATL_rzero};
      #endif
   #else
      TYPE one = ATL_rone;
      #ifdef BETA0
         const TYPE beta = ATL_rzero;
      #else
         const TYPE beta = ATL_rone;
      #endif
   #endif
   TYPE *A, *A0, *X, *Y, *y;
   int incY=1;
   TYPE *Y0;
   ATL_CINT aincY = Mabs(incY), incX=1, aincX=1;
   int ierr;

   fprintf(stdout, "   TEST M=%d, N=%d, lda=%d, STARTED\n", M, N, lda);
   A = FA_malloc(ATL_MulBySize(lda)*N, FAa, MAa);
   Y = FA_malloc(ATL_MulBySize(NY)*aincY, FAy, MAy);
   X = FA_malloc(ATL_MulBySize(NX), FAx, MAx);
   Y0 = FA_malloc(ATL_MulBySize(NY)*aincY, FAy, MAy);
   ATL_assert(A && Y0 && X && Y);
   if (aincX != 1)
      Mjoin(PATLU,set)(NX*aincX SHIFT, 4000000000.0, X, 1);
   if (aincY != 1)
   {
      Mjoin(PATLU,set)(NY*aincY SHIFT, 2000000000.0, Y0, 1);
      Mjoin(PATLU,set)(NY*aincY SHIFT, 3000000000.0, Y, 1);
   }
   Mjoin(PATL,gegen)(1, NY, Y0, aincY, NY*aincY);
   Mjoin(PATL,gegen)(1, NY, Y, aincY, NY*aincY);
   Mjoin(PATL,gegen)(1, NX, X, aincX, NY*aincY+127*50+77);
   Mjoin(PATL,gegen)(M, N, A, lda, N*M+513*7+90);
   if (incY < 0) Y += (NY-1) * (aincY SHIFT);

   dumb_gemv(0, M, N, one, A, lda, X, 1, beta, Y0, 1);
   ATL_UGEMV(M, N, A, lda, X, Y);
   ierr = CheckAns(M, N, Y0, 1, Y, 1);
   fprintf(stdout, "   TEST M=%d, N=%d, be=%.2f, lda=%d, incXY=%d,%d %s\n",
           M, N, SVAL beta, lda, incX, incY, (ierr)?"FAILED":"PASSED");
   FA_free(A, FAa, MAa);
   if (incY < 0)
   {
      Y -= (NY-1) * (aincY SHIFT);
      Y0 -= (NY-1) * (aincY SHIFT);
   }
   FA_free(Y0, FAy, MAy);
   FA_free(Y, FAy, MAy);
   if (incX < 0) X -= (NX-1) * (aincX SHIFT);
   FA_free(X, FAx, MAx);
   return(ierr);
}
#undef NX
#undef NY

int RunTests(int verb, int *Ms, int *Ns, int *ldas)
{
   int iy, ix, ic, in, im;
   ATL_INT m, n, lda, conj, incy;
   int nerr=0;
   assert(ldas[0] == Ms[0]);
   for (in=1; in <= Ns[0]; in++)
   {
      n = Ns[in];
      for (im=1; im <= Ms[0]; im++)
      {
         m = Ms[im];
         lda = ldas[im];
         nerr += RunTest(m, n, lda);
      }
   }
   return(nerr);
}

void PrintUsage(char *name, int ierr, char *flag)
{
   if (ierr > 0)
      fprintf(stderr, "Bad argument #%d: '%s'\n",
              ierr, flag ? flag : "Not enough arguments");
   else if (ierr < 0)
      fprintf(stderr, "ERROR: %s\n", flag);

   fprintf(stderr, "USAGE: %s [flags]:\n", name);
   fprintf(stderr, "   -n <#> <N1> ... <N#>\n");
   fprintf(stderr, "   -N <Nstart> <Nend> <Ninc>\n");
   fprintf(stderr, "   -m <#> <M1> ... <M#>\n");
   fprintf(stderr, "   -M <Mstart> <Mend> <Minc>\n");
   fprintf(stderr, "   -l <#> <lda1> ... <lda#>\n");
   fprintf(stderr, "   -g <ldagap> : lda = M + <ldagap> foreach M\n");
   fprintf(stderr,
           "   -v [0,1] : 0 - stop on first error, else keep testing\n");
   fprintf(stderr,
"   -F[x,y,a] <#> : if(# > 0) -> force op to be aligned to at least # bytes\n");
   fprintf(stderr,
      "                   if(# < 0) -> force op to be aligned to < # bytes.\n");

   exit(ierr ? ierr : -1);
}


/* procedure 1 */
int *GetIntList1(int ival)
/*
 * returns integer array with iarr[0] = 1, iarr[1] = ival
 */
{
   int *iarr;
   iarr = malloc(2*sizeof(int));
   ATL_assert(iarr);
   iarr[0] = 1;
   iarr[1] = ival;
   return(iarr);
}

#ifdef TYPE
/* procedure 2 */
TYPE *GetTypeList1(const SCALAR val)
/*
 * Returns a TYPE array with arr[0] = 1.0, arr[1] = val
 */
{
   TYPE *arr;
   arr = malloc(ATL_MulBySize(2));
   ATL_assert(arr);
   arr[0] = 1;
   #ifdef TCPLX
      arr[2] = *val;
      arr[3] = val[1];
   #else
      arr[1] = val;
   #endif
   return(arr);
}
#endif

/* procedure 3 */
int *GetIntList2(int ival1, int ival2)
/*
 * returns integer array with iarr[0] = 1, iarr[1] = ival1, ival[2] = ival2
 */
{
   int *iarr;
   iarr = malloc(3*sizeof(int));
   ATL_assert(iarr);
   iarr[0] = 1;
   iarr[1] = ival1;
   iarr[2] = ival2;
   return(iarr);
}

/* procedure 4 */
int *DupIntList(int *list)
/*
 * Duplicates list of integers, list[0] holds the length, not including 0
 */
{
   int i, n, *ip;

   assert(list);
   n = list[0] + 1;
   ip = malloc(sizeof(int)*n);
   assert(ip);
   for (i=0; i < n; i++)
      ip[i] = list[i];
   return(ip);
}

/* procedure 5 */
int *GetIntList(int nargs, char **args, int i, int nmul)
/*
 * Gets a list of integers, whose length is given by atoi(args[i])*nmul
 * list is this length+1, since 0'th location gets atoi(args[i])
 */
{
   int n, *iarr, k;

   if (++i >= nargs)
      PrintUsage(args[0], i, NULL);
   n = atoi(args[i]) * nmul;
   ATL_assert(n > 0);
   iarr = malloc(sizeof(int)*(n+1));
   ATL_assert(iarr);

   iarr[0] = n / nmul;
   for (k=0; k < n; k++)
   {
      if (++i >= nargs)
         PrintUsage(args[0], i, NULL);
      iarr[k+1] = atoi(args[i]);
   }
   return(iarr);
}

#ifdef TYPE
/* procedure 6 */
TYPE *GetTypeList(int nargs, char **args, int i, int nmul)
/*
 * Gets a list of TYPEs, whose length is given by atoi(args[i])*nmul
 * list is this length+1, since 0'th location gets atof(args[i])
 */
{
   int n, k;
   TYPE *arr;

   if (++i >= nargs)
      PrintUsage(args[0], i, NULL);
   n = atoi(args[i]) * nmul;
   ATL_assert(n > 0);
   arr = malloc(ATL_MulBySize(n+1));
   ATL_assert(arr);

   arr[0] = n / nmul;
   for (k=0; k < n; k++)
   {
      if (++i >= nargs)
         PrintUsage(args[0], i, NULL);
      arr[k+(1 SHIFT)] = atof(args[i]);
   }
   return(arr);
}
#endif

/* procedure 7 */
int *IntRange2IntList(int N0, int NN, int incN)
{
   int i, n;
   int *iarr;

   for (i=N0, n=0; i <= NN; i += incN) n++;
   iarr = malloc(sizeof(int)*(n+1));
   ATL_assert(iarr);
   iarr[0] = n;
   for (i=N0, n=1 ; i <= NN; i += incN, n++)
      iarr[n] = i;
   return(iarr);
}

int GetFlags(int nargs, char **args, int **Ms, int **Ns, int **LDAs)
{
   int verb, i, k, *ip;
   char ch;
   int ldagap = 8;

   *Ns = *Ms = *LDAs = NULL;
   verb = 0;

   for (i=1; i < nargs; i++)
   {
      if (args[i][0] != '-')
         PrintUsage(args[0], i, args[i]);
      ch = args[i][1];
      switch(ch)
      {
      case 'v':
        if (++i >= nargs)
            PrintUsage(args[0], i-1, "out of flags in -g ");
         verb = atoi(args[i]);
         break;
      case 'g':
        if (++i >= nargs)
            PrintUsage(args[0], i-1, "out of flags in -g ");
         ldagap = atoi(args[i]);
         break;
      case 'M':
      case 'N':
         if (i+3 >= nargs)
            PrintUsage(args[0], i-1, "out of flags in -N/M ");
         ip = IntRange2IntList(atoi(args[i+1]),atoi(args[i+2]),atoi(args[i+3]));
         if (ch == 'M')
            *Ms = ip;
         else
            *Ns = ip;
         i += 3;
         break;
      case 'n':
      case 'm':
      case 'l':
         ip = GetIntList(nargs, args, i, 1);
         i += ip[0] + 1;
         switch(ch)
         {
         case 'n':
            *Ns = ip;
            break;
         case 'm':
            *Ms = ip;
            break;
         case 'l':
            *LDAs = ip;
            break;
         }
         break;
      case 'F':
         ch = tolower(args[i][2]);
         k = atoi(args[++i]);
         if (ch == 'a')
         {
            if (k < 0)
              MAa = -k;
            else
              FAa = k;
         }
         else if (ch == 'y')
         {
            if (k < 0)
              MAy = -k;
            else
              FAy = k;
         }
         else
         {
            if (k < 0)
              MAx = -k;
            else
              FAx = k;
         }
         break;
      default:
         PrintUsage(args[0], i, args[i]);
      }
   }
   if (*Ms == NULL)
      *Ms = GetIntList1(977);
   if (*Ns == NULL)
      *Ns = GetIntList1(77);
   if (*LDAs == NULL)
   {
      *LDAs = DupIntList(*Ms);
      for (i=1; i <= (*LDAs)[0]; i++)
         (*LDAs)[i] += ldagap;
   }
   assert((*LDAs)[0] == (*Ms)[0]);
   return(verb);
}

int main(int nargs, char **args)
{
   int *Ms, *Ns, *LDAs, *incYs, *incXs, *CONJs;
   int verb, ierr=0;

   verb = GetFlags(nargs, args, &Ms, &Ns, &LDAs);
   ierr = RunTests(verb, Ms, Ns, LDAs);
   free(Ms);
   free(Ns);
   free(LDAs);
   exit(ierr);
}
