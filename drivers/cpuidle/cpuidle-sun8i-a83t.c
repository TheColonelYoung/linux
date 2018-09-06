// SPDX-License-Identifier: GPL-2.0
/*
 * CPU idle for Allwinner A83t SoC
 *
 * This driver is used because standart ARM idle is not working for A83t 
 * due to not supported PSCI. This driver has only WFI state.
 */

#define pr_fmt(fmt) "CPUidle Allwinner A83t: " fmt

#include <linux/cpu_pm.h>
#include <linux/cpuidle.h>
#include <linux/cpumask.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <asm/cpuidle.h>
#include <asm/mcpm.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>


static int notrace allwinner_a83t_core_sleep_finisher(unsigned long arg)
{

	unsigned int mpidr = read_cpuid_mpidr();
	unsigned int cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	unsigned int cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);


	pr_info("CPU: %d CLS: %d FNC: %s: Core sleep suspend", cpu, cluster, __func__);

	mcpm_set_entry_vector(cpu, cluster, cpu_resume);

	mcpm_cpu_suspend();

	return 1;
	
}

static int allwinner_a83t_core_sleep(struct cpuidle_device *dev,
				struct cpuidle_driver *drv, int idx)
{
	unsigned int mpidr = read_cpuid_mpidr();
	unsigned int cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	unsigned int cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);

	local_irq_disable();

	if(dev->cpu == 0){
		cpu_do_idle();
	}else{
		
		pr_info("CPU: %d CLS: %d FNC: %s: Core sleep", cpu, cluster, __func__);

		cpu_pm_enter();

		cpu_suspend(0, allwinner_a83t_core_sleep_finisher);

		pr_info("CPU: %d CLS: %d FNC: %s: Power up", cpu, cluster, __func__);

		mcpm_cpu_powered_up();

		cpu_pm_exit();

		
	}

	local_irq_enable();

	return idx;
}

static struct cpuidle_driver allwinner_a83t_idle_driver = {
	.name			= "allwinner_a83t_idle",
	.owner			= THIS_MODULE,
	.states = {
		[0] = ARM_CPUIDLE_WFI_STATE
	},
	.states[1] = {
		.enter                  = allwinner_a83t_core_sleep,
		.exit_latency           = 3000,
		.target_residency       = 10000,
		.power_usage		= 500,
		.flags			= CPUIDLE_FLAG_TIMER_STOP,
		.name                   = "C1",
		.desc                   = "Core power down",
	},
	.state_count = 2,
	.safe_state_index = 0,
};

static int __init allwinner_a83t_cpuidle_probe(void)
{
	int cpu, ret;
	struct cpuidle_driver *drv;
	struct cpuidle_device *dev;

	pr_info("MCPM availability: %d",mcpm_is_available());

	for_each_possible_cpu(cpu) {

		drv = kmemdup(&allwinner_a83t_idle_driver, sizeof(*drv),
                                 GFP_KERNEL);
		if (!drv) {
			ret = -ENOMEM;
			goto out_fail;
		}

		drv->cpumask = (struct cpumask *)cpumask_of(cpu);

		ret = cpuidle_register_driver(drv);
		if (ret) {
			pr_err("Failed to register cpuidle driver %d\n",ret);
			goto out_fail;
		}

		dev = kzalloc(sizeof(*dev), GFP_KERNEL);
		if (!dev) {
			ret = -ENOMEM;
			goto out_fail;
		}
		dev->cpu = cpu;

		ret = cpuidle_register_device(dev);
		if (ret) {
			pr_err("Failed to register cpuidle device for CPU %d\n",
			       cpu);
			goto out_fail;
		}

		pr_info("Successfully register idle driver for CPU%d\n", cpu);
	}

	pr_info("Idle states count: %d", allwinner_a83t_idle_driver.state_count);

	return 0;

out_fail:
	while (--cpu >= 0) {
		dev = per_cpu(cpuidle_devices, cpu);
		cpuidle_unregister_device(dev);
		kfree(dev);
		drv = cpuidle_get_driver();
		cpuidle_unregister_driver(drv);
		kfree(drv);
	}
	return ret;
}

device_initcall(allwinner_a83t_cpuidle_probe);