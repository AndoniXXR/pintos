/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value)
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0)
    {
    // Inserta el hilo actual en la lista de espera del semáforo, ordenándolo según la prioridad
    // Esto garantiza que el hilo de mayor prioridad esté al principio de la lista y se despierte primero
    list_insert_ordered (&sema->waiters, &thread_current ()->elem, (list_less_func *) &priority_comparator, NULL);

      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema)
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0)
    {
      sema->value--;
      success = true;
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();



// Verifica si la lista de espera del semáforo no está vacía
struct list_elem *father_element;
if (!list_empty (&sema->waiters)){

  // Ordena la lista de espera para asegurarse de que esté en orden descendente de prioridad
  list_sort(&sema->waiters, (list_less_func *) &priority_comparator, NULL);

  // Extrae el elemento al frente de la lista (mayor prioridad) y desbloquea el hilo asociado
  father_element = list_pop_front(&sema->waiters);
  thread_unblock(list_entry(father_element, struct thread, elem));
}


  sema->value++;
  intr_set_level (old_level);
  if (!intr_context())
    thread_yield();
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void)
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++)
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_)
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++)
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);

}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire(struct lock *lock)
{
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(!lock_held_by_current_thread(lock));


  enum intr_level old_level;
  old_level = intr_disable();

  // Comprueba si el candado no está siendo sostenido por ningún hilo
  if (lock->holder == NULL)
    thread_current()->locker_thread = NULL;  // Ningún hilo tiene el candado; establece locker_thread como NULL

  else  // Algún hilo ya tiene este candado
  {
    thread_current()->locker_thread = lock->holder;   // El titular actual del candado está bloqueando el hilo actual
    list_push_front(&lock->holder->donation_list, &thread_current()->donorelem);
    // Dona prioridad al titular del candado al colocar el hilo actual en la donation_list del titular

    struct thread *cur_thread = thread_current();
    cur_thread->waiting_on_lock = lock;  // Establece waiting_on_lock para este candado

    while (cur_thread->locker_thread != NULL)
    {
      /*
      Si la prioridad del hilo actual es mayor que la prioridad del adquiridor del candado,
      dona prioridad al adquiridor del candado para que termine su ejecución y libere el candado
      */
      if (cur_thread->priority > cur_thread->locker_thread->priority)
      {
        cur_thread->locker_thread->priority = cur_thread->priority;  // $$$$$ DONACIÓN DE PRIORIDAD REAL AQUÍ $$$$$$
        cur_thread = cur_thread->locker_thread;  // Ahora el hilo en ejecución es el hilo adquiriendo el candado
      }
    }
  }


  // Adquiere el semáforo asociado con el candado
  sema_down(&lock->semaphore);
  lock->holder = thread_current();  // Establece el hilo actual como el titular del candado
  intr_set_level(old_level);
}


/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire(struct lock *lock)
{
  ASSERT(lock != NULL);
  ASSERT(!lock_held_by_current_thread(lock));

  // Intenta bajar el semáforo asociado al candado
  bool success = sema_try_down(&lock->semaphore);

  if (!success) {
    // Si no se pudo adquirir el candado, intenta adquirirlo utilizando la función lock_acquire
    lock_acquire(lock);
  } else {
    // Si se adquirió el candado exitosamente, establece al hilo actual como su titular
    lock->holder = thread_current();
  }

  return success;  // Retorna true si se adquirió el candado exitosamente, false de lo contrario
}



/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release(struct lock *lock)
{
  // Asegurarse de que el candado y el hilo actual sean válidos
  ASSERT(lock != NULL);
  ASSERT(lock_held_by_current_thread(lock));

  // Liberar el candado y permitir que otros hilos lo adquieran
  lock->holder = NULL;
  sema_up(&lock->semaphore);


  // Deshabilitar las interrupciones para realizar operaciones críticas
  enum intr_level old_level;
  old_level = intr_disable();

  // Verificar si hay donadores de prioridad
  if (!list_empty(&thread_current()->donation_list))
  {
    // Iterar sobre la lista de donadores de prioridad
    struct list_elem *iter;
    for (iter = list_begin(&thread_current()->donation_list); iter != list_end(&thread_current()->donation_list); iter = list_next(iter))
    {
      // Si el donador estaba esperando por este candado, quitarlo de la lista de donadores
      if (list_entry(iter, struct thread, donorelem)->waiting_on_lock == lock)
      {
        list_remove(iter);
        list_entry(iter, struct thread, donorelem)->waiting_on_lock = NULL; // Desbloquear al donador
      }
      else
        continue;
    }

    // Encontrar el donador de mayor prioridad
    struct thread *max_donor = list_entry(list_begin(&thread_current()->donation_list), struct thread, donorelem);

    for (iter = list_begin(&thread_current()->donation_list); iter != list_end(&thread_current()->donation_list); iter = list_next(iter))
    {
      // Actualizar al donador de mayor prioridad si se encuentra un donador con una prioridad más alta
      if (list_entry(iter, struct thread, donorelem)->priority > max_donor->priority)
        max_donor = list_entry(iter, struct thread, donorelem);
    }

    // Actualizar la prioridad del hilo actual si es menor que la prioridad del donador máximo
    if (thread_current()->basepriority < max_donor->priority)
    {
      thread_current()->priority = max_donor->priority;
      thread_yield(); // Ceder el procesador al hilo de mayor prioridad
    }
    else
    {
      thread_set_priority(thread_current()->basepriority);
    }
  }
  else
  {
    thread_set_priority(thread_current()->basepriority); // Restaurar la prioridad original si no hay donadores
  }

  // Restablecer las interrupciones a su nivel original
  intr_set_level(old_level);

  intr_set_level(old_level);
}




/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock)
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock)
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  sema_init (&waiter.semaphore, 0);

  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
  // Asegurarse de que los argumentos sean válidos
  ASSERT(cond != NULL);
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(lock_held_by_current_thread(lock));

  // Verificar si hay hilos esperando en la condición
  if (!list_empty(&cond->waiters))
  {

    // Ordenar la lista de hilos en espera por prioridad utilizando conditional_var_comparator
    list_sort(&cond->waiters, (list_less_func *)&conditional_var_comparator, NULL);


    // Despertar al hilo de mayor prioridad esperando en la variable de condición
    sema_up(&list_entry(list_pop_front(&cond->waiters), struct semaphore_elem, elem)->semaphore);
  }
}



/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}


bool conditional_var_comparator(struct list_elem *a, struct list_elem *b, void *aux) {
  // Extraer los elementos del semáforo de cada lista
  struct semaphore_elem *semaphore_one = list_entry(a, struct semaphore_elem, elem);
  struct semaphore_elem *semaphore_two = list_entry(b, struct semaphore_elem, elem);

  // Obtener los hilos del frente de la lista de espera de cada semáforo
  struct thread *s_one = list_entry(list_front(&semaphore_one->semaphore.waiters), struct thread, elem);
  struct thread *s_two = list_entry(list_front(&semaphore_two->semaphore.waiters), struct thread, elem);

  // Comparar las prioridades de los hilos para determinar el orden
  if (s_one->priority > s_two->priority) {
    // El hilo en el elemento 'a' tiene mayor prioridad, debe despertarse primero
    return true;
  } else {
    // El hilo en el elemento 'b' tiene mayor prioridad o las prioridades son iguales,
    // debería estar delante o en la misma posición
    return false;
  }
}

