
#ifndef __TASK_PARAMS_H__
#define __TASK_PARAMS_H__

struct TaskParams
{
  const char * ip_;
  int          port_;
  int          messageSize_;
}; // struct TaskParams
typedef struct TaskParams TaskParams;

int ProcessCmdLine(TaskParams * taskParams, int argc, const char * argv[]);

#endif
