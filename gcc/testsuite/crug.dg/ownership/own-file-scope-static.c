/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* A function-local 'own' with static storage duration is a documented
   limitation: the declaration is tracked but the object outlives the call.
   Only file-scope declarations are rejected (crug-OWN-014).  */

void local_static_own (void)
{
  static own int *held;
}
