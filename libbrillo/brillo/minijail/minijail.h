// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_MINIJAIL_MINIJAIL_H_
#define LIBBRILLO_BRILLO_MINIJAIL_MINIJAIL_H_

#include <vector>

extern "C" {
#include <linux/capability.h>
#include <sys/types.h>
}

#include <base/lazy_instance.h>
#include <brillo/brillo_export.h>

#include <libminijail.h>

namespace brillo {

// DEPRECATED. Use libminijail.h instead.
// TODO(crbug.com/992135): Deprecate |Minijail| with attribute and completely
// remove all use cases.
//
// A Minijail abstraction allowing Minijail mocking in tests.
class BRILLO_EXPORT Minijail {
 public:
  virtual ~Minijail();

  // This is a singleton -- use Minijail::GetInstance()->Foo().
  static Minijail* GetInstance();

  // minijail_new
  virtual struct minijail* New();
  // minijail_destroy
  virtual void Destroy(struct minijail* jail);

  // minijail_change_uid/minijail_change_gid
  virtual void DropRoot(struct minijail* jail, uid_t uid, gid_t gid);

  // minijail_change_user/minijail_change_group
  virtual bool DropRoot(struct minijail* jail,
                        const char* user,
                        const char* group);

  // minijail_namespace_pids
  virtual void EnterNewPidNamespace(struct minijail* jail);

  // minijail_mount_tmp
  virtual void MountTmp(struct minijail* jail);

  // minijail_use_seccomp_filter/minijail_no_new_privs/
  // minijail_parse_seccomp_filters
  virtual void UseSeccompFilter(struct minijail* jail, const char* path);

  // minijail_use_caps
  virtual void UseCapabilities(struct minijail* jail, uint64_t capmask);

  // minijail_reset_signal_mask
  virtual void ResetSignalMask(struct minijail* jail);

  // minijail_close_open_fds
  virtual void CloseOpenFds(struct minijail* jail);

  // minijail_preserve_fd
  virtual void PreserveFd(struct minijail* jail, int parent_fd, int child_fd);

  // minijail_enter
  virtual void Enter(struct minijail* jail);

  // minijail_run_pid
  virtual bool Run(struct minijail* jail, std::vector<char*> args, pid_t* pid);

  // minijail_run_pid and waitpid
  virtual bool RunSync(struct minijail* jail,
                       std::vector<char*> args,
                       int* status);

  // minijail_run_pid_pipes, with |pstdout_fd| and |pstderr_fd| set to NULL.
  virtual bool RunPipe(struct minijail* jail,
                       std::vector<char*> args,
                       pid_t* pid,
                       int* stdin);

  // minijail_run_pid_pipes
  virtual bool RunPipes(struct minijail* jail,
                        std::vector<char*> args,
                        pid_t* pid,
                        int* stdin,
                        int* stdout,
                        int* stderr);

  // minijail_run_env_pid_pipes
  virtual bool RunEnvPipes(struct minijail* jail,
                           std::vector<char*> args,
                           std::vector<char*> env,
                           pid_t* pid,
                           int* stdin,
                           int* stdout,
                           int* stderr);
  // Run() and Destroy()
  virtual bool RunAndDestroy(struct minijail* jail,
                             std::vector<char*> args,
                             pid_t* pid);

  // RunSync() and Destroy()
  virtual bool RunSyncAndDestroy(struct minijail* jail,
                                 std::vector<char*> args,
                                 int* status);

  // RunPipe() and Destroy()
  virtual bool RunPipeAndDestroy(struct minijail* jail,
                                 std::vector<char*> args,
                                 pid_t* pid,
                                 int* stdin);

  // RunPipes() and Destroy()
  virtual bool RunPipesAndDestroy(struct minijail* jail,
                                  std::vector<char*> args,
                                  pid_t* pid,
                                  int* stdin,
                                  int* stdout,
                                  int* stderr);

  // RunEnvPipes() and Destroy()
  virtual bool RunEnvPipesAndDestroy(struct minijail* jail,
                                     std::vector<char*> args,
                                     std::vector<char*> env,
                                     pid_t* pid,
                                     int* stdin,
                                     int* stdout,
                                     int* stderr);

 protected:
  Minijail();
  Minijail(const Minijail&) = delete;
  Minijail& operator=(const Minijail&) = delete;

 private:
  friend base::LazyInstanceTraitsBase<Minijail>;
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_MINIJAIL_MINIJAIL_H_
