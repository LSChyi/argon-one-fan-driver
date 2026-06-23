#include <linux/err.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/thermal.h>

struct argon_one_fan_data {
  struct i2c_client *client;
  struct device *hwmon_dev;
  struct thermal_cooling_device *cdev; /* Thermal cooling device handle */
  struct mutex update_lock;
  u8 device_val;
};

/* Internal helper to send speed to the physical hardware (0-100) */
static int argon_one_fan_set_hardware_speed(struct argon_one_fan_data *data,
                                            u8 device_val) {
  int ret;

  ret = i2c_smbus_write_byte(data->client, device_val);
  if (ret < 0) {
    dev_err(&data->client->dev, "I2C Write failed\n");
    return ret;
  }
  data->device_val = device_val;
  return 0;
}

static int argon_one_fan_get_max_state(struct thermal_cooling_device *cdev,
                                       unsigned long *state) {
  *state = 10;
  return 0;
}

static int argon_one_fan_get_cur_state(struct thermal_cooling_device *cdev,
                                       unsigned long *state) {
  struct argon_one_fan_data *data = cdev->devdata;

  mutex_lock(&data->update_lock);
  *state = data->device_val / 10;
  mutex_unlock(&data->update_lock);

  return 0;
}

static int argon_one_fan_set_cur_state(struct thermal_cooling_device *cdev,
                                       unsigned long state) {
  struct argon_one_fan_data *data = cdev->devdata;
  int ret;

  if (state > 10)
    return -EINVAL;

  mutex_lock(&data->update_lock);
  ret = argon_one_fan_set_hardware_speed(data, state * 10);
  mutex_unlock(&data->update_lock);

  return ret;
}

static const struct thermal_cooling_device_ops argon_one_fan_cooling_ops = {
    .get_max_state = argon_one_fan_get_max_state,
    .get_cur_state = argon_one_fan_get_cur_state,
    .set_cur_state = argon_one_fan_set_cur_state,
};

static ssize_t set_pwm(struct device *dev, struct device_attribute *attr,
                       const char *buf, size_t count) {
  struct argon_one_fan_data *data = dev_get_drvdata(dev);
  long val;
  int ret;

  if (kstrtol(buf, 10, &val) < 0)
    return -EINVAL;

  val = clamp_val(val, 0, 255);

  mutex_lock(&data->update_lock);
  ret = argon_one_fan_set_hardware_speed(data,
                                         (u8)(((u16)val * 100 + 127) / 255));
  mutex_unlock(&data->update_lock);

  return ret < 0 ? ret : count;
}

static ssize_t show_pwm(struct device *dev, struct device_attribute *attr,
                        char *buf) {
  struct argon_one_fan_data *data = dev_get_drvdata(dev);
  return sprintf(buf, "%d\n", ((u16)data->device_val * 100 + 127) / 255);
}

static SENSOR_DEVICE_ATTR(pwm1, 0644, show_pwm, set_pwm, 0);
static struct attribute *argon_one_fan_attrs[] = {
    &sensor_dev_attr_pwm1.dev_attr.attr, NULL};
ATTRIBUTE_GROUPS(argon_one_fan);

static int argon_one_fan_probe(struct i2c_client *client) {
  struct argon_one_fan_data *data;
  int ret;

  if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE))
    return -EIO;

  data =
      devm_kzalloc(&client->dev, sizeof(struct argon_one_fan_data), GFP_KERNEL);
  if (!data)
    return -ENOMEM;

  data->client = client;
  mutex_init(&data->update_lock);
  data->device_val = 0;

  /* Register Hwmon */
  data->hwmon_dev = devm_hwmon_device_register_with_groups(
      &client->dev, "argon_one_fan", data, argon_one_fan_groups);
  if (IS_ERR(data->hwmon_dev))
    return PTR_ERR(data->hwmon_dev);

  /* Register as a Linux Thermal Cooling Device linked to Device Tree */
  data->cdev = devm_thermal_of_cooling_device_register(
      &client->dev, client->dev.of_node, "argon_one_fan", data,
      &argon_one_fan_cooling_ops);
  if (IS_ERR(data->cdev)) {
    dev_err(&client->dev, "Failed to register thermal cooling device\n");
    return PTR_ERR(data->cdev);
  }

  i2c_set_clientdata(client, data);

  ret = argon_one_fan_set_hardware_speed(data, 0);
  if (ret) {
    dev_err(&client->dev, "Failed to initialize fan state to 0: %d\n", ret);
    return ret;
  }

  dev_info(&client->dev, "Argon One Fan Driver with Thermal Support Loaded\n");
  return 0;
}

static const struct i2c_device_id argon_one_fan_id[] = {{"argon_one_fan", 0},
                                                        {}};
MODULE_DEVICE_TABLE(i2c, argon_one_fan_id);

static struct i2c_driver argon_one_fan_driver = {
    .driver =
        {
            .name = "argon_one_fan",
        },
    .probe = argon_one_fan_probe,
    .id_table = argon_one_fan_id,
};

module_i2c_driver(argon_one_fan_driver);

MODULE_AUTHOR("Sung Chi Li");
MODULE_DESCRIPTION("argon One Fan Driver with Thermal Management");
MODULE_LICENSE("GPL");
