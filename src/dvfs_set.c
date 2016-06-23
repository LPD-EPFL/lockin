#include "dvfs_set.h"

__thread int dvfs_cid;
__thread int dvfs_sys_fd = -1;

int
dvfs_freq_init(int cid)
{
  dvfs_cid = cid;
  char buf[64];
  int fd;
  snprintf(buf, sizeof(buf), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_setspeed", cid);
  fd = open(buf, O_RDWR);
  if (fd < 0)
    {
      fprintf(stdout, "Failed to open cpufreq file %s: %s", buf, strerror(errno));
      return EXIT_FAILURE;
    }
  dvfs_sys_fd = fd;
  return EXIT_SUCCESS;
}

int
dvfs_freq_term(int cpu)
{
  if (dvfs_sys_fd >= 0)
    {
      return close(dvfs_sys_fd);
    }
  return 0;
}

int
dvfs_freq_set_cpu(int cpu, const char* freq_khz)
{
  char buf[64];
  int fd;
  int ret = EXIT_SUCCESS;
  snprintf(buf, sizeof(buf), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_setspeed", cpu);
  fd = open(buf, O_RDWR);
  if (fd < 0)
    {
      fprintf(stdout, "Failed to open cpufreq file %s: %s", buf, strerror(errno));
      return EXIT_FAILURE;
    }

  if (write(fd, freq_khz, strlen(freq_khz)) != (int) strlen(freq_khz))
    {
      fprintf(stdout, "Failed to write to cpufreq file.\n");
      ret = EXIT_FAILURE;
    }

  close(fd);
  return ret;
}

int
dvfs_freq_set(const char* freq)
{
  int ret = EXIT_SUCCESS;
  if (write(dvfs_sys_fd, freq, 8) == 8)
    {
      fdatasync(dvfs_sys_fd);
    }
  else
    {
      fprintf(stdout, "Failed to write to cpufreq file.\n");
      ret = EXIT_FAILURE;
    }
 
  return ret;
}

int
dvfs_freq_set_all_max()
{
    int hw_ctx_n = sysconf(_SC_NPROCESSORS_ONLN);
    int c, ret = 0;
    for (c = 0; c < hw_ctx_n; c++)
      {
	ret |= (dvfs_freq_set_cpu(c, dvfs_freq_max) != 0);
      }

    if (ret)
      {
	ret = -1;
      }
    return ret;
}

int
dvfs_freq_set_range(int core_from, int core_to, dvfs_setting_t setting)
{
  if (core_to <= 0)
    {
      core_to = sysconf(_SC_NPROCESSORS_ONLN);
    }
  int c, ret = 0;
  for (c = core_from; c < core_to; c++)
    {
      if (setting == DVFS_FREQ_MAX)
	{
	  ret |= (dvfs_freq_set_cpu(c, dvfs_freq_max) != 0);
	}
      else
	{
	  ret |= (dvfs_freq_set_cpu(c, dvfs_freq_min) != 0);
	}
    }

  if (ret)
    {
      ret = -1;
    }
  return ret;

}
