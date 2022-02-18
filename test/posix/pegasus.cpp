
#include <catch_config.h>
#include <test_utils.h>

#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

#if defined(ATHENA_PRELOAD)
#include <athena/api/posix.h>
#else
#include <fcntl.h>
#endif
/**
 * Test data structures
 */
namespace mimir::test {
struct Arguments {
  fs::path pfs = "/home/haridev/pfs";
  fs::path shm = "/dev/shm/haridev";
  std::string filename = "test.dat";
  size_t request_size = 65536;
  size_t iteration = 64;
};
}  // namespace mimir::test

mimir::test::Arguments args;

/**
 * Overridden methods for catch
 */

int init(int* argc, char*** argv) {
  // MPI_Init(argc, argv);
  return 0;
}
int finalize() {
  // MPI_Finalize();
  return 0;
}

cl::Parser define_options() {
  return cl::Opt(args.filename, "filename")["-f"]["--filename"](
             "Filename to be use for I/O.") |
         cl::Opt(args.pfs, "pfs")["-p"]["--pfs"](
             "Directory used for performing I/O (default pfs)") |
         cl::Opt(args.shm, "shm")["-s"]["--shm"](
             "Directory used for performing I/O (default shm)") |
         cl::Opt(args.request_size, "request_size")["-r"]["--request_size"](
             "Transfer size used for performing I/O") |
         cl::Opt(args.iteration,
                 "iteration")["-i"]["--iteration"]("Number of Iterations");
}

/**
 * utility methods
 */
inline std::string GetFilenameFromFD(int fd) {
  const int kMaxSize = 0xFFF;
  char proclnk[kMaxSize];
  char filename[kMaxSize];
  snprintf(proclnk, kMaxSize, "/proc/self/fd/%d", fd);
  size_t r = readlink(proclnk, filename, kMaxSize);
  filename[r] = '\0';
  return filename;
}

/**
 * Test cases
 */

TEST_CASE("Write",
          "[operation=write]"
          "[request_size=" +
              std::to_string(args.request_size) +
              "]"
              "[iteration=" +
              std::to_string(args.iteration) + "]") {
  Timer initialization, metadata, io, finalization;

  initialization.resumeTime();
  fs::path filepath = args.pfs / args.filename;
  fs::create_directories(args.pfs);
  /** Clean existing file**/
  if (fs::exists(filepath)) fs::remove(filepath);
  /** Prepare data **/
  auto write_data = std::vector<char>(args.request_size, 'w');
  initialization.pauseTime();

#ifdef ATHENA_PRELOAD
  using namespace mimir;
  MimirHandler job_configuration_handler;
  JobConfigurationAdvice job_conf_advice;
  job_conf_advice._job_id = 0;
  job_conf_advice._devices.emplace_back(args.shm, 16);
  job_conf_advice._devices.emplace_back(args.pfs, 128);
  job_conf_advice._job_time_minutes = 30;
  job_conf_advice._num_cores_per_node = 8;
  job_conf_advice._num_gpus_per_node = 0;
  job_conf_advice._num_nodes = 1;
  job_conf_advice._priority = 100;
  job_configuration_advice_begin(job_conf_advice, job_configuration_handler);

  MimirHandler file_handler;
  FileAdvice file_advice;
  file_advice._type._secondary = OperationAdviceType::INDEPENDENT_FILE;
  file_advice._per_io_data = args.iteration / (args.iteration + 2);
  file_advice._per_io_metadata = 2 / (args.iteration + 2);
  file_advice._size_mb = args.request_size * args.iteration / MB;
  file_advice._device = Storage(args.pfs, 128);

  if (args.request_size >= 0 && args.request_size < 4 * KB)
    file_advice._write_distribution._0_4kb = 1.0;
  else if (args.request_size >= 4 * KB && args.request_size < 64 * KB)
    file_advice._write_distribution._4_64kb = 1.0;
  if (args.request_size >= 64 * KB && args.request_size < 1 * MB)
    file_advice._write_distribution._64kb_1mb = 1.0;
  if (args.request_size >= 1 * MB && args.request_size < 16 * MB)
    file_advice._write_distribution._1mb_16mb = 1.0;
  if (args.request_size >= 16 * MB) file_advice._write_distribution._16mb = 1.0;

  file_advice._io_amount_mb = args.request_size * args.iteration / MB;
  file_advice._format = Format::FORMAT_BINARY;
  file_advice._priority = 100;
  file_advice._name = filepath;
  file_advice_begin(file_advice, file_handler);
#endif

  /** Main I/O **/
  metadata.resumeTime();
  int fd =
      open(filepath.c_str(), O_WRONLY | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
  metadata.pauseTime();
  REQUIRE(fd != -1);

  for (size_t i = 0; i < args.iteration; ++i) {
    io.resumeTime();
    ssize_t bytes_written = write(fd, write_data.data(), args.request_size);
    int fsync_status = fsync(fd);
    io.pauseTime();
    REQUIRE(bytes_written == args.request_size);
  }
  finalization.resumeTime();
  auto new_file = GetFilenameFromFD(fd);
  finalization.pauseTime();
  metadata.resumeTime();
  int close_status = close(fd);
  metadata.pauseTime();
  REQUIRE(close_status == 0);

  REQUIRE(fs::file_size(new_file) == args.request_size * args.iteration);

  finalization.resumeTime();
  printf("I/O performed on file %s\n", new_file.c_str());
  finalization.pauseTime();
#if defined(ATHENA_PRELOAD)
  file_advice_end(file_handler);
#endif
  fprintf(stdout, "Timing: init %f, metadata %f, io %f, and finalize %f.\n",
          initialization.getElapsedTime(), metadata.getElapsedTime(),
          io.getElapsedTime(), finalization.getElapsedTime());
}

TEST_CASE("Read",
          "[operation=read]"
          "[request_size=" +
              std::to_string(args.request_size) +
              "]"
              "[iteration=" +
              std::to_string(args.iteration) + "]") {
  Timer initialization, metadata, io, finalization;

  initialization.resumeTime();
  fs::path filepath = args.pfs / args.filename;
  /** Prepare data **/
  auto read_data = std::vector<char>(args.request_size, 'r');
  initialization.pauseTime();
  fprintf(stdout, "file to read %s\n", filepath.c_str());
#ifdef ATHENA_PRELOAD
  using namespace mimir;
  MimirHandler job_configuration_handler;
  JobConfigurationAdvice job_conf_advice;
  job_conf_advice._job_id = 0;
  job_conf_advice._devices.emplace_back(args.shm, 16);
  job_conf_advice._devices.emplace_back(args.pfs, 128);
  job_conf_advice._job_time_minutes = 30;
  job_conf_advice._num_cores_per_node = 8;
  job_conf_advice._num_gpus_per_node = 0;
  job_conf_advice._num_nodes = 1;
  job_conf_advice._priority = 100;
  job_configuration_advice_begin(job_conf_advice, job_configuration_handler);

  MimirHandler file_handler;
  FileAdvice file_advice;
  file_advice._type._secondary = OperationAdviceType::INDEPENDENT_FILE;
  file_advice._per_io_data = args.iteration / (args.iteration + 2);
  file_advice._per_io_metadata = 2 / (args.iteration + 2);
  file_advice._size_mb = args.request_size * args.iteration / MB;
  file_advice._device = Storage(args.pfs, 128);

  if (args.request_size >= 0 && args.request_size < 4 * KB)
    file_advice._write_distribution._0_4kb = 1.0;
  else if (args.request_size >= 4 * KB && args.request_size < 64 * KB)
    file_advice._write_distribution._4_64kb = 1.0;
  if (args.request_size >= 64 * KB && args.request_size < 1 * MB)
    file_advice._write_distribution._64kb_1mb = 1.0;
  if (args.request_size >= 1 * MB && args.request_size < 16 * MB)
    file_advice._write_distribution._1mb_16mb = 1.0;
  if (args.request_size >= 16 * MB) file_advice._write_distribution._16mb = 1.0;

  file_advice._io_amount_mb = args.request_size * args.iteration / MB;
  file_advice._format = Format::FORMAT_BINARY;
  file_advice._priority = 100;
  file_advice._name = filepath;
  file_advice_begin(file_advice, file_handler);
#endif
  /** Main I/O **/
  metadata.resumeTime();
  int fd = open(filepath.c_str(), O_RDONLY);
  metadata.pauseTime();
  REQUIRE(fd != -1);

  for (size_t i = 0; i < args.iteration; ++i) {
    io.resumeTime();
    ssize_t bytes_read = read(fd, read_data.data(), args.request_size);
    int fsync_status = fsync(fd);
    io.pauseTime();
    REQUIRE(bytes_read == args.request_size);
  }

  finalization.resumeTime();
  auto new_file = GetFilenameFromFD(fd);
  finalization.pauseTime();

  metadata.resumeTime();
  int close_status = close(fd);
  metadata.pauseTime();
  REQUIRE(close_status == 0);

  REQUIRE(fs::file_size(new_file) == args.request_size * args.iteration);

  finalization.resumeTime();
  printf("I/O performed on file %s\n", new_file.c_str());
  finalization.pauseTime();
#if defined(ATHENA_PRELOAD)
  file_advice_end(file_handler);
#endif
  fprintf(stdout, "Timing: init %f, metadata %f, io %f, and finalize %f.\n",
          initialization.getElapsedTime(), metadata.getElapsedTime(),
          io.getElapsedTime(), finalization.getElapsedTime());
}