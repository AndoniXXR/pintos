#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <user/syscall.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

#define MAX_ARGS 3


static void syscall_handler (struct intr_frame *);
int add_file (struct file *file_name);
void get_args (struct intr_frame *f, int *arg, int num_of_args);
void syscall_halt (void);
pid_t syscall_exec(const char* cmdline);
int syscall_wait(pid_t pid);
bool syscall_create(const char* file_name, unsigned starting_size);
bool syscall_remove(const char* file_name);
int syscall_open(const char * file_name);
int syscall_filesize(int filedes);
int syscall_read(int filedes, void *buffer, unsigned length);
int syscall_write (int filedes, const void * buffer, unsigned byte_size);
void syscall_seek (int filedes, unsigned new_position);
unsigned syscall_tell(int fildes);
void syscall_close(int filedes);
void validate_ptr (const void* vaddr);
void validate_str (const void* str);
void validate_buffer (const void* buf, unsigned byte_size);

bool FILE_LOCK_INIT = false;

/*
 * Inicializador de llamada al sistema
 * Se encarga de la configuración de las operaciones de llamada al sistema
 */
void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/*
 * Este método se encarga de varios casos de comandos del sistema
 * Este manejador invoca la llamada a la función adecuada que debe realizarse
 * en la línea de comandos
 */
static void
syscall_handler (struct intr_frame *f UNUSED)
{
  if (!FILE_LOCK_INIT)
  {
    lock_init(&file_system_lock);
    FILE_LOCK_INIT = true;
  }

  int arg[MAX_ARGS];
  int esp = getpage_ptr((const void *) f->esp);

  switch (* (int *) esp)
  {
    case SYS_HALT:
      syscall_halt();
      break;

    case SYS_EXIT:
      /* Rellena los arg con la cantidad de argumentos necesarios */
      get_args(f, &arg[0], 1);
      syscall_exit(arg[0]);
      break;

    case SYS_EXEC:
      /* Rellena los arg con la cantidad de argumentos necesarios */
      get_args(f, &arg[0], 1);

      /* Obtiene un puntero de pagina */
      arg[0] = getpage_ptr((const void *)arg[0]);
      /* syscall_exec(const char* cmdline) */
      f->eax = syscall_exec((const char*)arg[0]); /* Ejecuta el comando de linea*/
      break;

    case SYS_OPEN:
      /* Rellena los arg con la cantidad de argumentos necesarios */
      get_args(f, &arg[0], 1);

      /* Comprueba si la linea de comandos es valida
       * No se abre basura que pueda causar algun tipo de fallo
       */
       validate_str((const void*)arg[0]);

     /* Obtiene un puntero de la pagina */
      arg[0] = getpage_ptr((const void *)arg[0]);

      /* syscall_open(int filedes) */
      f->eax = syscall_open((const char *)arg[0]);  /* Abre este archivo */
      break;

    case SYS_WRITE:

      /* Rellena los arg con la cantidad de argumentos necesarios */
      get_args(f, &arg[0], 3);

      /* Comprueba si el búfer es válido
       * No se mete con un búfer que está fuera
       * Memoria virtual reservada
       */
       validate_buffer((const void*)arg[1], (unsigned)arg[2]);

      /* Obtiene un puntero de la pagina*/
      arg[1] = getpage_ptr((const void *)arg[1]);

      /* syscall_write (int filedes, const void * buffer, unsigned bytes)*/
      f->eax = syscall_write(arg[0], (const void *) arg[1], (unsigned) arg[2]);
      break;

    default:
      break;
  }
}

/* halt */
void
syscall_halt (void)
{
  shutdown_power_off(); /* Llamada a shutdown.h */
}

/* Obtiene argumentos de la pila */
void
get_args (struct intr_frame *f, int *args, int num_of_args)
{
  int i;
  int *ptr;
  for (i = 0; i < num_of_args; i++)
  {
    ptr = (int *) f->esp + i + 1;
    validate_ptr((const void *) ptr);
    args[i] = *ptr;
  }
}

/* Salida de llamada del sistema
 * Comprueba si el thread actual para salir es un hijo
 * Si es así, se actualiza la información de los padres del hijo.
 */
void
syscall_exit (int status)
{
  struct thread *cur = thread_current();
  if (is_thread_alive(cur->parent) && cur->cp)
  {
    if (status < 0)
    {
      status = -1;
    }
    cur->cp->status = status;
  }
  printf("%s: exit(%d)\n", cur->name, status);
  thread_exit();
}

/* syscall exec
 * Ejecuta la línea de comandos y regresa
 * El pid del thread que se está ejecutando actualmente
 * Comando
 */
pid_t
syscall_exec(const char* cmdline)
{
    pid_t pid = process_execute(cmdline);
    struct child_process *child_process_ptr = find_child_process(pid);
    if (!child_process_ptr)
    {
      return ERROR;
    }
    /* Comprueba si el proceso está cargado */
    if (child_process_ptr->load_status == NOT_LOADED)
    {
      sema_down(&child_process_ptr->load_sema);
    }
    /* Comprueba si el proceso no se ha cargado */
    if (child_process_ptr->load_status == LOAD_FAIL)
    {
      remove_child_process(child_process_ptr);
      return ERROR;
    }
    return pid;
}

/* syscall_create */
bool
syscall_create(const char* file_name, unsigned starting_size)
{
  lock_acquire(&file_system_lock);
  bool successful = filesys_create(file_name, starting_size); /* Llamada a filesys.h */
  lock_release(&file_system_lock);
  return successful;
}

/* syscall_open */
int
syscall_open(const char *file_name)
{
  lock_acquire(&file_system_lock);
  struct file *file_ptr = filesys_open(file_name); /* Llamada a filesys.h */
  if (!file_ptr)
  {
    lock_release(&file_system_lock);
    return ERROR;
  }
  int filedes = add_file(file_ptr);
  lock_release(&file_system_lock);
  return filedes;
}

/* syscall_read */
#define STD_INPUT 0
#define STD_OUTPUT 1
int
syscall_read(int filedes, void *buffer, unsigned length)
{
  if (length <= 0)
  {
    return length;
  }

  if (filedes == STD_INPUT)
  {
    unsigned i = 0;
    uint8_t *local_buf = (uint8_t *) buffer;
    for (;i < length; i++)
    {
      /* Recuperar la tecla pulsada del búfer de entrada */
      local_buf[i] = input_getc(); /* Llamada a input.h */
    }
    return length;
  }

  /* Lee de file */
  lock_acquire(&file_system_lock);
  struct file *file_ptr = get_file(filedes);
  if (!file_ptr)
  {
    lock_release(&file_system_lock);
    return ERROR;
  }
  int bytes_read = file_read(file_ptr, buffer, length); /* Llamada a file.h */
  lock_release (&file_system_lock);
  return bytes_read;
}

/* syscall_write */
int
syscall_write (int filedes, const void * buffer, unsigned byte_size)
{
    if (byte_size <= 0)
    {
      return byte_size;
    }
    if (filedes == STD_OUTPUT)
    {
      putbuf (buffer, byte_size); /* De stdio.h */
      return byte_size;
    }

    /* Empieza a escribir en el archivo */
    lock_acquire(&file_system_lock);
    struct file *file_ptr = get_file(filedes);
    if (!file_ptr)
    {
      lock_release(&file_system_lock);
      return ERROR;
    }
    int bytes_written = file_write(file_ptr, buffer, byte_size); /* Llamada a file.h */
    lock_release (&file_system_lock);
    return bytes_written;
}

/* Función para comprobar si el puntero es válido */
void
validate_ptr (const void *vaddr)
{
    if (vaddr < USER_VADDR_BOTTOM || !is_user_vaddr(vaddr))
    {
      /* La dirección de memoria virtual no está reservada para nosotros (out of bound) */
      syscall_exit(ERROR);
    }
}

/* Función para comprobar si la cadena es válida */
void
validate_str (const void* str)
{
    for (; * (char *) getpage_ptr(str) != 0; str = (char *) str + 1);
}

/* Función para comprobar si el búfer es válido */
void
validate_buffer(const void* buf, unsigned byte_size)
{
  unsigned i = 0;
  char* local_buffer = (char *)buf;
  for (; i < byte_size; i++)
  {
    validate_ptr((const void*)local_buffer);
    local_buffer++;
  }
}

/* Obtiene el puntero a la página */
int
getpage_ptr(const void *vaddr)
{
  void *ptr = pagedir_get_page(thread_current()->pagedir, vaddr);
  if (!ptr)
  {
    syscall_exit(ERROR);
  }
  return (int)ptr;
}

/* Encontrar un proceso hijo basado en el pid */
struct child_process* find_child_process(int pid)
{
  struct thread *t = thread_current();
  struct list_elem *e;
  struct list_elem *next;

  for (e = list_begin(&t->child_list); e != list_end(&t->child_list); e = next)
  {
    next = list_next(e);
    struct child_process *cp = list_entry(e, struct child_process, elem);
    if (pid == cp->pid)
    {
      return cp;
    }
  }
  return NULL;
}

/* Eliminar un proceso hijo específico */
void
remove_child_process (struct child_process *cp)
{
  list_remove(&cp->elem);
  free(cp);
}

/* Eliminar todos los procesos hijos de un thread */
void remove_all_child_processes (void)
{
  struct thread *t = thread_current();
  struct list_elem *next;
  struct list_elem *e = list_begin(&t->child_list);

  for (;e != list_end(&t->child_list); e = next)
  {
    next = list_next(e);
    struct child_process *cp = list_entry(e, struct child_process, elem);
    list_remove(&cp->elem); /* Remueve el proceso hijo */
    free(cp);
  }
}

/* Añade un archivo a la lista de archivos y devuelve el descriptor del archivo añadido */
int
add_file (struct file *file_name)
{
  struct process_file *process_file_ptr = malloc(sizeof(struct process_file));
  if (!process_file_ptr)
  {
    return ERROR;
  }
  process_file_ptr->file = file_name;
  process_file_ptr->fd = thread_current()->fd;
  thread_current()->fd++;
  list_push_back(&thread_current()->file_list, &process_file_ptr->elem);
  return process_file_ptr->fd;
}

/* Obtiene el archivo que coincide con el descriptor del archivo */
struct file*
get_file (int filedes)
{
  struct thread *t = thread_current();
  struct list_elem* next;
  struct list_elem* e = list_begin(&t->file_list);

  for (; e != list_end(&t->file_list); e = next)
  {
    next = list_next(e);
    struct process_file *process_file_ptr = list_entry(e, struct process_file, elem);
    if (filedes == process_file_ptr->fd)
    {
      return process_file_ptr->file;
    }
  }
  return NULL; /* No se ha encontrado nada */
}

/* Cierra el descriptor del fichero deseado */
void
process_close_file (int file_descriptor)
{
  struct thread *t = thread_current();
  struct list_elem *next;
  struct list_elem *e = list_begin(&t->file_list);

  for (;e != list_end(&t->file_list); e = next)
  {
    next = list_next(e);
    struct process_file *process_file_ptr = list_entry (e, struct process_file, elem);
    if (file_descriptor == process_file_ptr->fd || file_descriptor == CLOSE_ALL_FD)
    {
      file_close(process_file_ptr->file);
      list_remove(&process_file_ptr->elem);
      free(process_file_ptr);
      if (file_descriptor != CLOSE_ALL_FD)
      {
        return;
      }
    }
  }
}
