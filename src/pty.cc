/**
 * pty.cc
 * This file is responsible for starting processes
 * with pseudo-terminal file descriptors.
 *
 * man tty_ioctl
 * man tcsetattr
 * man forkpty
 */

#include <v8.h>
#include <node.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>

/* forkpty */
/* http://www.gnu.org/software/gnulib/manual/html_node/forkpty.html */
#if defined(__GLIBC__) || defined(__CYGWIN__)
  #include <pty.h>
#elif defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)
  #include <util.h>
#elif defined(__FreeBSD__)
  #include <libutil.h>
#else
  #include <pty.h>
#endif

#include <utmp.h> /* login_tty */
#include <termios.h> /* tcgetattr, tty_ioctl */

using namespace std;
using namespace node;
using namespace v8;

static Handle<Value> ForkPty(const Arguments&);
static Handle<Value> ResizePty(const Arguments&);
extern "C" void init(Handle<Object>);

static Handle<Value> ForkPty(const Arguments& args) {
  HandleScope scope;

  char *argv[] = { "sh", NULL };

  if (args.Length() < 2) 
    {
      return ThrowException(Exception::Error(
	      String::New("Not enough arguments to ForkPty.")));
    }
  if (!args[0]->IsString() || !args[1]->IsString()) 
    {
      return ThrowException(Exception::Error(
	      String::New("First two arguments must be strings.")));
    }
    String::Utf8Value file(args[0]->ToString());
    argv[0] = strdup(*file);

  String::Utf8Value username(args[1]->ToString());
  struct passwd *pwd = getpwnam(*username);

  struct winsize winp = {};
  winp.ws_col = 80;
  winp.ws_row = 30;

  if (args.Length() == 5) {
    if (args[3]->IsNumber() && args[4]->IsNumber()) {
      Local<Integer> cols = args[3]->ToInteger();
      Local<Integer> rows = args[4]->ToInteger();

      winp.ws_col = cols->Value();
      winp.ws_row = rows->Value();
    } else {
      return ThrowException(Exception::Error(
        String::New("cols and rows need to be numbers.")));
    }
  }

  int master;
  signal(SIGCHLD, SIG_IGN);

  pid_t pid = forkpty(&master, NULL, NULL, &winp);

  if (pid == -1) {
    return ThrowException(Exception::Error(
      String::New("forkpty failed.")));
  }

  if (pid == 0) {
    setgid(pwd->pw_gid);
    setegid(pwd->pw_gid);

    gid_t newgid = getgid();
    setgroups(1, &newgid);

    setuid(pwd->pw_uid);
    seteuid(pwd->pw_uid);

    if (args.Length() > 2 && args[2]->IsString()) {
      String::Utf8Value term(args[2]->ToString());
      setenv("TERM", strdup(*term), 1);
    } else {
      setenv("TERM", "vt100", 1);
    }

    chdir(pwd->pw_dir);
    execvp(argv[0], argv);

    perror("execvp failed");
    _exit(1);
  }

  Local<Object> obj = Object::New();
  obj->Set(String::New("fd"), Number::New(master));
  obj->Set(String::New("pid"), Number::New(pid));

  return scope.Close(obj);
}

/**
 * Expose Resize Functionality
 */

static Handle<Value> ResizePty(const Arguments& args) {
  HandleScope scope;

  if (args.Length() > 0 && !args[0]->IsNumber()) {
    return ThrowException(Exception::Error(
      String::New("First argument must be a number.")));
  }

  struct winsize winp = {};
  winp.ws_col = 80;
  winp.ws_row = 30;

  int fd = args[0]->ToInteger()->Value();

  if (args.Length() == 3) {
    if (args[1]->IsNumber() && args[2]->IsNumber()) {
      Local<Integer> cols = args[1]->ToInteger();
      Local<Integer> rows = args[2]->ToInteger();

      winp.ws_col = cols->Value();
      winp.ws_row = rows->Value();
    } else {
      return ThrowException(Exception::Error(
        String::New("cols and rows need to be numbers.")));
    }
  }

  if (ioctl(fd, TIOCSWINSZ, &winp) == -1) {
    return ThrowException(Exception::Error(
      String::New("ioctl failed.")));
  }

  return Undefined();
}

extern "C" void init(Handle<Object> target) {
  HandleScope scope;
  NODE_SET_METHOD(target, "fork", ForkPty);
  NODE_SET_METHOD(target, "resize", ResizePty);
}
