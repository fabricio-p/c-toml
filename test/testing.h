#include <CUnit/Console.h>
#include <CUnit/Automated.h>
#include <CUnit/Basic.h>
#include <stdio.h>
#include <stdlib.h>

#define RUN_TESTS                                               \
  char option;                                                  \
  if (argc > 1)                                                 \
  {                                                             \
    option = *argv[1];                                          \
    goto test_runer;                                            \
  }                                                             \
  printf("(A)utomated (C)onsole (B)asic\n");                    \
  printf("Enter test runing method (default Basic): ");         \
  option = getc(stdin);                                         \
  option = option == '\n' ? 'B': option;                        \
  test_runer:                                                   \
  printf("Selected option (%c)\n", option);                     \
  switch (option)                                               \
  {                                                             \
    case 'a':                                                   \
    case 'A':                                                   \
  CU_automated_run_tests();                                     \
  break;                                                        \
    case 'c':                                                   \
    case 'C':                                                   \
  CU_console_run_tests();                                       \
  break;                                                        \
    case 'b':                                                   \
    case 'B':                                                   \
  CU_basic_run_tests();                                         \
  break;/*                                                      \
    case 'u':                                                   \
    case 'U':                                                   \
  CU_curses_run_tests();                                        \
  break;*/                                                      \
    default:                                                    \
  status = 1;                                                   \
  fprintf(stderr, "Unknown option [%d](%c)\n", option, option); \
  goto cleanup;                                                 \
  }
