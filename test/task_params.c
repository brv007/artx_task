#include "task_params.h"

#include <stdio.h>
#include <getopt.h>
#include <error.h>
#include <stdlib.h>
#include <ctype.h>


static void print_help(const char * programName)
{
  fprintf(stdout, "Использование: %s [опции]\n"
                  "опции:\n"
                  "	-?	--help		эта справка\n"
                  "	-h	--host		IP адрес сервера (127.0.0.1)\n"
                  "	-p	--port		порт сервера (1032)\n"
                  "	-s	--data-size	размер сообщения (64 байта)\n", programName);
}

int ProcessCmdLine(TaskParams * taskParams, int argc, const char * argv[])
{
  int c;
  int ret = 0;

  taskParams->ip_   = "127.0.0.1";
  taskParams->port_ = 1032;
  taskParams->messageSize_ = 64;

  while (1)
  {
    int option_index = 0;
    static struct option long_options[] =
                     {
                         {"help",      no_argument,       0, '?'},
                         {"host",      required_argument, 0, 'h'},
                         {"port",      required_argument, 0, 'p'},
                         {"data-size", required_argument, 0, 's'},
                         {0, 0, 0, 0},
                     };

    c = getopt_long (argc, (char * const *)(argv), "?h:p:s:", long_options, &option_index);
    if (c == -1)
    {
      break;
    }
    switch (c)
    {
      case '?':
        print_help(argv[0]);
        ret = 1;
        break;

      case 'h':
        {
          int a, b, c, d;
          if (sscanf(optarg, "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
          {
            error(EXIT_FAILURE, 0, "Неверный формат IP адреса: '%s'", optarg);
          }
          taskParams->ip_ = optarg;
          break;
        }

      case 'p':
        for (int i = 0; optarg[i] != 0; ++i)
        {
          if (!isdigit(optarg[i]))
          {
            error(EXIT_FAILURE, 0, "Недопустимый символ в номера порта: '%s'", optarg);
          }
        }
        if ((taskParams->port_ = atoi(optarg)) <= 0)
        {
          error(EXIT_FAILURE, 0, "Некорректное значение порта");
        }
        break;

      case 's':
        for (int i = 0; optarg[i] != 0; ++i)
        {
          if (!isdigit(optarg[i]))
          {
            error(EXIT_FAILURE, 0, "Недопустимый символ в размере сообщения: '%s'", optarg);
          }
        }
        if ((taskParams->messageSize_ = atoi(optarg)) <= 0)
        {
          error(EXIT_FAILURE, 0, "Некорректное значение размера сообщения");
        }
        break;

      default:
        error(EXIT_FAILURE, 0, "Неверный параметр командной строки:\n"
                               "\t\tиспользуйте параметр --help для справки\n");
        break;
    }
  }
  return ret;
}

