#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#define LCD_RS 0x01
#define LCD_EN 0x04
#define LCD_BL 0x08
#define LCD_D4 0x10
#define LCD_D5 0x20
#define LCD_D6 0x40
#define LCD_D7 0x80

static struct i2c_client *lcd_client;

#define LCD_TEXT_MAX 256
#define LCD_PAGE_LEN 32
#define LCD_PAGE_DELAY msecs_to_jiffies(5000)

struct lcd_data {
	struct i2c_client *client;

	char text[LCD_TEXT_MAX];
	size_t text_len;
	size_t page;

	struct delayed_work display_work;
};

static struct lcd_data *lcd;

/*
 * Send one byte to the I2C backpack.
 */
static int lcd_write(struct i2c_client *client, u8 data)
{
	int ret;

	ret = i2c_smbus_write_byte(client, data);

	if (ret < 0)
		dev_err(&client->dev,
			"GDM1602A: I2C write failed: %d\n", ret);

	return ret;
}

/*
 * Send one 4-bit nibble to the HD44780 LCD.
 */
static int lcd_send_nibble(struct i2c_client *client,
			   u8 nibble, bool rs)
{
	u8 data = LCD_BL;
	int ret;

	if (rs)
		data |= LCD_RS;

	if (nibble & 0x01)
		data |= LCD_D4;

	if (nibble & 0x02)
		data |= LCD_D5;

	if (nibble & 0x04)
		data |= LCD_D6;

	if (nibble & 0x08)
		data |= LCD_D7;

	/* Put data on the bus */
	ret = lcd_write(client, data);
	if (ret < 0)
		return ret;

	/* Enable pulse */
	ret = lcd_write(client, data | LCD_EN);
	if (ret < 0)
		return ret;

	udelay(1);

	/* Disable pulse */
	ret = lcd_write(client, data);
	if (ret < 0)
		return ret;

	udelay(50);

	return 0;
}

/*
 * Send one complete byte to the LCD.
 *
 * LCD works in 4-bit mode, therefore each byte
 * is transmitted as two nibbles.
 */
static int lcd_send_byte(struct i2c_client *client,
			 u8 value, bool rs)
{
	int ret;

	ret = lcd_send_nibble(client,
			      (value >> 4) & 0x0f,
			      rs);
	if (ret < 0)
		return ret;

	ret = lcd_send_nibble(client,
			      value & 0x0f,
			      rs);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * Send command to LCD.
 */
static int lcd_command(struct i2c_client *client, u8 cmd)
{
	return lcd_send_byte(client, cmd, false);
}

/*
 * Send character/data to LCD.
 */
static int lcd_data(struct i2c_client *client, u8 data)
{
	return lcd_send_byte(client, data, true);
}

static int lcd_create_char(struct i2c_client *client,
			   u8 index, const u8 *pattern)
{
	int ret;
	int i;

	if (index >= 8)
		return -EINVAL;

	/* Set CGRAM address */
	ret = lcd_command(client, 0x40 | (index << 3));
	if (ret < 0)
		return ret;

	/* Write 8 rows of the custom character */
	for (i = 0; i < 8; i++) {
		ret = lcd_data(client, pattern[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const u8 a_ogonek[8] = {
	0x00,
	0x0E,
	0x01,
	0x0F,
	0x11,
	0x0F,
	0x02,
	0x01,
};

static const u8 c_caron[8] = {
	0b01010,
	0b00100,
	0b00000,
	0b01110,
	0b10000,
	0b10000,
	0b01110,
	0b00000
};

static const u8 e_ogonek[8] = {
	0b00000,
	0b01110,
	0b10001,
	0b11111,
	0b10000,
	0b01111,
	0b00010,
	0b00001
};

static const u8 e_dot[8] = {
	0b00100,
	0b00000,
	0b01110,
	0b10001,
	0b11111,
	0b10000,
	0b01110,
	0b00000
};

static const u8 i_ogonek[8] = {
	0b00100,
	0b00000,
	0b01100,
	0b00100,
	0b00100,
	0b01110,
	0b00100,
	0b00010
};

static const u8 s_caron[8] = {
	0b01010,
	0b00100,
	0b00000,
	0b01110,
	0b11000,
	0b00110,
	0b11100,
	0b00000
};

static const u8 u_ogonek[8] = {
	0b00000,
	0b10001,
	0b10001,
	0b10001,
	0b10011,
	0b01101,
	0b00010,
	0b00001
};

static const u8 u_macron[8] = {
	0b01110,
	0b00000,
	0b10001,
	0b10001,
	0b10001,
	0b10011,
	0b01101,
	0b00000
};

/*
 * Initialize HD44780 LCD in 4-bit mode.
 */
static int lcd_init(struct i2c_client *client)
{
	int ret;

	dev_info(&client->dev,
		 "GDM1602A: starting LCD initialization\n");

	msleep(20);

	/*
	 * HD44780 4-bit initialization sequence.
	 */

	ret = lcd_send_nibble(client, 0x03, false);
	if (ret < 0)
		return ret;

	msleep(5);

	ret = lcd_send_nibble(client, 0x03, false);
	if (ret < 0)
		return ret;

	udelay(150);

	ret = lcd_send_nibble(client, 0x03, false);
	if (ret < 0)
		return ret;

	ret = lcd_send_nibble(client, 0x02, false);
	if (ret < 0)
		return ret;

	/*
	 * Function set:
	 * 4-bit mode
	 * 2 lines
	 * 5x8 font
	 */
	ret = lcd_command(client, 0x28);
	if (ret < 0)
		return ret;

	/* Display OFF */
	ret = lcd_command(client, 0x08);
	if (ret < 0)
		return ret;

	/* Clear display */
	ret = lcd_command(client, 0x01);
	if (ret < 0)
		return ret;

	msleep(2);

	/* Entry mode: cursor moves right */
	ret = lcd_command(client, 0x06);
	if (ret < 0)
		return ret;

	/* Display ON, cursor OFF, blink OFF */
	ret = lcd_command(client, 0x0c);
	if (ret < 0)
		return ret;

    
    /*
    * Create custom Lithuanian character 'ą'
    * in CGRAM position 0.
    */
    ret = lcd_create_char(client, 0, a_ogonek);
    if (ret < 0)
	    return ret;

    ret = lcd_create_char(client, 1, c_caron);
    if (ret < 0)
	    return ret;

    ret = lcd_create_char(client, 2, e_ogonek);
    if (ret < 0)
	    return ret;

    ret = lcd_create_char(client, 3, e_dot);
    if (ret < 0)
	    return ret;

    ret = lcd_create_char(client, 4, i_ogonek);
    if (ret < 0)
	    return ret;

    ret = lcd_create_char(client, 5, s_caron);
    if (ret < 0)
	    return ret;

    ret = lcd_create_char(client, 6, u_ogonek);
    if (ret < 0)
	    return ret;
    
    ret = lcd_create_char(client, 7, u_macron);
    if (ret < 0)
	    return ret;
	
        /* Grįžtame į DDRAM, kad galėtume rodyti tekstą */
    ret = lcd_command(client, 0x80);
    if (ret < 0)
	    return ret;

    dev_info(&client->dev,
	     "GDM1602A: LCD initialization completed\n");

    return 0;
    }

static int lcd_write_text(struct i2c_client *client,
			  const char *text, size_t len)
{
	size_t i = 0;
	int ret;

	while (i < len) {

		/*
		 * Lietuviškos raidės UTF-8 formatu.
		 */

		/* ą = C4 85 -> CGRAM 0 */
		if (i + 1 < len &&
		    (u8)text[i] == 0xC4 &&
		    (u8)text[i + 1] == 0x85) {

			ret = lcd_data(client, 0);
			if (ret < 0)
				return ret;

			i += 2;
			continue;
		}

		/* č = C4 8D -> CGRAM 1 */
		if (i + 1 < len &&
		    (u8)text[i] == 0xC4 &&
		    (u8)text[i + 1] == 0x8D) {

			ret = lcd_data(client, 1);
			if (ret < 0)
				return ret;

			i += 2;
			continue;
		}

		/* ę = C4 99 -> CGRAM 2 */
		if (i + 1 < len &&
		    (u8)text[i] == 0xC4 &&
		    (u8)text[i + 1] == 0x99) {

			ret = lcd_data(client, 2);
			if (ret < 0)
				return ret;

			i += 2;
			continue;
		}

		/* ė = C4 97 -> CGRAM 3 */
		if (i + 1 < len &&
		    (u8)text[i] == 0xC4 &&
		    (u8)text[i + 1] == 0x97) {

			ret = lcd_data(client, 3);
			if (ret < 0)
				return ret;

			i += 2;
			continue;
		}

		/* į = C4 AF -> CGRAM 4 */
		if (i + 1 < len &&
		    (u8)text[i] == 0xC4 &&
		    (u8)text[i + 1] == 0xAF) {

			ret = lcd_data(client, 4);
			if (ret < 0)
				return ret;

			i += 2;
			continue;
		}

		/* š = C5 A1 -> CGRAM 5 */
		if (i + 1 < len &&
		    (u8)text[i] == 0xC5 &&
		    (u8)text[i + 1] == 0xA1) {

			ret = lcd_data(client, 5);
			if (ret < 0)
				return ret;

			i += 2;
			continue;
		}

		/* ų = C5 B3 -> CGRAM 6 */
		if (i + 1 < len &&
		    (u8)text[i] == 0xC5 &&
		    (u8)text[i + 1] == 0xB3) {

			ret = lcd_data(client, 6);
			if (ret < 0)
				return ret;

			i += 2;
			continue;
		}
        
        /* ū = C5 AB -> CGRAM 7 */
        if (i + 1 < len &&
            (u8)text[i] == 0xC5 &&
            (u8)text[i + 1] == 0xAB) {

	        ret = lcd_data(client, 7);
	        if (ret < 0)
		    return ret;

	    i += 2;
	    continue;
        }

		/*
		 * Paprastas ASCII simbolis.
		 */
		ret = lcd_data(client, (u8)text[i]);
		if (ret < 0)
			return ret;

		i++;
	}

	return 0;
}

static void lcd_display_work(struct work_struct *work)
{
	struct lcd_data *data =
		container_of(to_delayed_work(work),
			     struct lcd_data, display_work);

	size_t start;
	size_t i;
	int ret;

	start = data->page * LCD_PAGE_LEN;

	if (start >= data->text_len) {
		data->page = 0;
		start = 0;
	}

	/* Clear display */
	ret = lcd_command(data->client, 0x01);
	if (ret < 0)
		return;

	msleep(2);

	/* First row */
    ret = lcd_command(data->client, 0x80);
    if (ret < 0)
	    return;

    ret = lcd_write_text(data->client,
		     &data->text[start],
		     min_t(size_t, 16, data->text_len - start));
    if (ret < 0)
	    return;

    i = min_t(size_t, 16, data->text_len - start);

	/* Second row */
	ret = lcd_command(data->client, 0xc0);
	if (ret < 0)
		return;

	for (; i < LCD_PAGE_LEN && start + i < data->text_len; i++) {
		ret = lcd_data(data->client, data->text[start + i]);
		if (ret < 0)
			return;
	}

	/* Only cycle pages when text is longer than 32 characters */
	if (data->text_len > LCD_PAGE_LEN) {
		data->page++;

		schedule_delayed_work(&data->display_work,
				      LCD_PAGE_DELAY);
	} else {
		data->page = 0;
	}
}

/*
 * Write userspace data to LCD.
 *
 * Example:
 *
 *     echo "Labas" > /dev/lcd0
 */
static ssize_t lcd_write_file(struct file *file,
			      const char __user *buf,
			      size_t count,
			      loff_t *ppos)
{
	size_t len;

	if (count == 0)
		return 0;

	/*
	 * Apribojame teksto ilgį iki mūsų buferio dydžio.
	 */
	len = min(count, (size_t)LCD_TEXT_MAX - 1);

	/*
	 * Nukopijuojame tekstą iš userspace į kernelio buferį.
	 */
	if (copy_from_user(lcd->text, buf, len))
		return -EFAULT;

	lcd->text[len] = '\0';

	/*
	 * Pašaliname \n ir \r, kuriuos dažnai prideda echo.
	 */
	while (len > 0 &&
	       (lcd->text[len - 1] == '\n' ||
		lcd->text[len - 1] == '\r')) {
		lcd->text[len - 1] = '\0';
		len--;
	}

	lcd->text_len = len;

	/*
	 * Pradedame nuo pirmo 32 simbolių puslapio.
	 */
	lcd->page = 0;

	dev_info(&lcd->client->dev,
		 "GDM1602A: received %zu characters\n",
		 lcd->text_len);

	/*
	 * Atšaukiame ankstesnį suplanuotą work,
	 * jei toks dar yra.
	 */
	cancel_delayed_work_sync(&lcd->display_work);

	/*
	 * Iškart parodyti pirmą puslapį.
	 */
	schedule_delayed_work(&lcd->display_work, 0);

	return count;
}

/*
 * File operations for /dev/lcd0.
 */
static const struct file_operations lcd_fops = {
	.owner = THIS_MODULE,
	.write = lcd_write_file,
};

/*
 * Create /dev/lcd0.
 */
static struct miscdevice lcd_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lcd0",
	.fops = &lcd_fops,
};

/*
 * I2C probe.
 */
static int lcd_probe(struct i2c_client *client)
{
	int ret;

	lcd = devm_kzalloc(&client->dev,
			   sizeof(*lcd),
			   GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	lcd->client = client;
	lcd->text_len = 0;
	lcd->page = 0;

	INIT_DELAYED_WORK(&lcd->display_work,
			  lcd_display_work);
    
	dev_info(&client->dev,
		 "GDM1602A: probe\n");

	dev_info(&client->dev,
		 "GDM1602A: I2C address = 0x%02x\n",
		 client->addr);

	/*
	 * Save I2C client.
	 */
	lcd_client = client;

	/*
	 * Initialize LCD.
	 */
	ret = lcd_init(client);
	if (ret < 0) {
		dev_err(&client->dev,
			"GDM1602A: LCD initialization failed: %d\n",
			ret);

		lcd_client = NULL;

		return ret;
	}

	/*
	 * Register /dev/lcd0.
	 */
	ret = misc_register(&lcd_miscdev);
	if (ret < 0) {
		dev_err(&client->dev,
			"GDM1602A: failed to register /dev/lcd0: %d\n",
			ret);

		lcd_client = NULL;

		return ret;
	}

	dev_info(&client->dev,
		 "GDM1602A: /dev/lcd0 created\n");

	return 0;
}

/*
 * I2C remove.
 */
static void lcd_remove(struct i2c_client *client)
{
	cancel_delayed_work_sync(&lcd->display_work);

	misc_deregister(&lcd_miscdev);

	lcd_client = NULL;

	dev_info(&client->dev,
		 "GDM1602A: remove\n");
}

/*
 * Device Tree matching.
 */
static const struct of_device_id lcd_of_match[] = {
	{
		.compatible = "custom,gdm1602a"
	},
	{ }
};

MODULE_DEVICE_TABLE(of, lcd_of_match);

/*
 * I2C driver definition.
 */
static struct i2c_driver lcd_driver = {
	.driver = {
		.name = "gdm1602a",
		.of_match_table = lcd_of_match,
	},
	.probe = lcd_probe,
	.remove = lcd_remove,
};

module_i2c_driver(lcd_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vakarė");
MODULE_DESCRIPTION("GDM1602A I2C LCD driver");