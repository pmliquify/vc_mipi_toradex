#include "vc_mipi_subdev.h"
#include "vc_mipi_camera.h"
#include "vc_mipi_core.h"
#include <linux/delay.h>

#define TRACE printk("        TRACE [vc-mipi] vc_mipi_subdev.c --->  %s : %d", __FUNCTION__, __LINE__);
//#define TRACE


// --- v4l2_subdev_core_ops ---------------------------------------------------

int vc_mipi_s_power(struct v4l2_subdev *sd, int on)
{
	struct vc_mipi_camera *camera = to_vc_mipi_camera(sd);
	struct i2c_client *client_mod = camera->client_mod;
	struct device *dev_mod = &client_mod->dev;
	int ret;

	// dev_info(dev_mod, "[vc-mipi subdev] %s: On=%d\n", __FUNCTION__, on); 

	if (on) {
		/* restore controls */
		ret = v4l2_ctrl_handler_setup(sd->ctrl_handler);
		if(ret) {
			dev_err(dev_mod, "[vc-mipi subdev] %s: Failed to restore control handler\n",  __FUNCTION__);
		}
	}

	// TODO: Original implementation
	// priv->on = on;
	
	return ret;
}

int vc_mipi_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	return v4l2_queryctrl(sd->ctrl_handler, qc);
}

int vc_mipi_try_ext_ctrls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ctrls)
{
	return 0;
}

int vc_mipi_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *control)
{
	struct vc_mipi_camera *camera = to_vc_mipi_camera(sd);
	struct i2c_client *client_mod = camera->client_mod;
	struct i2c_client *client_sen = camera->client_sen;
	struct device* dev = sd->dev;
	struct v4l2_ctrl *ctrl;

	// Settings
					// 0=free run, 1=ext. trigger, 2=trigger self test
	int sensor_ext_trig = 0; 	// ext. trigger flag: 0=no, 1=yes
	int sen_clk = 54000000; 	// sen_clk default=54Mhz, imx183=72Mhz

	// TRACE

	ctrl = v4l2_ctrl_find(sd->ctrl_handler, control->id);
	if (ctrl == NULL)
		return -EINVAL;

	if(control->value < ctrl->minimum || control->value > ctrl->maximum) {
		dev_err(dev, "[vc-mipi subdev] %s: Control value '%s' %d exceeds allowed range (%lld - %lld)\n", 
			__FUNCTION__, ctrl->name, control->value, ctrl->minimum, ctrl->maximum);
		return -EINVAL;
	}

	// Store value in ctrl to get and restore ctrls later and after power off.
	v4l2_ctrl_s_ctrl(ctrl, control->value);

     	switch (ctrl->id)  {
 	case V4L2_CID_EXPOSURE:
		if (sensor_ext_trig) { 
			return vc_mipi_mod_set_exposure(client_mod, control->value, sen_clk);
		} else {
	        	return vc_mipi_sen_set_exposure(client_sen, control->value);
		}

	case V4L2_CID_GAIN:
		return vc_mipi_sen_set_gain(client_sen, control->value);
    	}

       	dev_err(dev, "[vc-mipi subdev] %s: ctrl(id:0x%x,val:0x%x) is not handled\n", __FUNCTION__, ctrl->id, ctrl->val);
      	return -EINVAL;
}

int vc_mipi_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *control)
{
	return v4l2_g_ctrl(sd->ctrl_handler, control);
}


// --- v4l2_subdev_video_ops ---------------------------------------------------

int vc_mipi_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	TRACE

	return 0;
}

int vc_mipi_s_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
 	TRACE
	
	return 0;
}

int vc_mipi_s_stream(struct v4l2_subdev *sd, int enable)
{
 	TRACE
	
	return 0;
}


// --- v4l2_subdev_pad_ops ---------------------------------------------------

int vc_mipi_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_mbus_code_enum *code)
{
	struct vc_mipi_camera *camera = to_vc_mipi_camera(sd);
	
	TRACE
	
    	if (code->pad || code->index >= camera->vc_mipi_data_fmts_size)
        	return -EINVAL;

    	code->code = 			camera->vc_mipi_data_fmts[code->index].code;

	return 0;
}

int vc_mipi_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *format)
{
	struct vc_mipi_camera *camera = to_vc_mipi_camera(sd);
	struct v4l2_mbus_framefmt *mf = &format->format;

	TRACE

	if (format->pad) {
		return -EINVAL;
	}

	mf->code = 			camera->fmt->code;
	mf->colorspace  = 		camera->fmt->colorspace;
	mf->field = V4L2_FIELD_NONE;
	mf->width = 			camera->pix.width;
	mf->height = 			camera->pix.height;

	return 0;
}

static const struct vc_mipi_datafmt *vc_mipi_find_datafmt(struct v4l2_subdev *sd, __u32 code)
{
	struct vc_mipi_camera *camera = to_vc_mipi_camera(sd);
	int i;

	for (i = 0; i < camera->vc_mipi_data_fmts_size; i++) {
		if (camera->vc_mipi_data_fmts[i].code == code) {
			return &camera->vc_mipi_data_fmts[i];
		}
	}
	return NULL;
}

static int get_capturemode(struct v4l2_subdev *sd, int width, int height)
{
	int i;
	
	for (i = 0; i < ARRAY_SIZE(vc_mipi_valid_res); i++) {
		if ((vc_mipi_valid_res[i].width == width) && (vc_mipi_valid_res[i].height == height)) {
		return i;
		}
	}
	return -1;
}

int vc_mipi_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *format)
{
	struct vc_mipi_camera *camera = to_vc_mipi_camera(sd);
	struct device *dev = sd->dev;
	struct v4l2_mbus_framefmt *mf = &format->format;
    	const struct vc_mipi_datafmt *fmt = vc_mipi_find_datafmt(sd, mf->code);
	int capturemode;

	TRACE

	if (!fmt) {
		mf->code = 		camera->vc_mipi_data_fmts[0].code;
		mf->colorspace = 	camera->vc_mipi_data_fmts[0].colorspace;
		fmt = 		       &camera->vc_mipi_data_fmts[0];
	}

	mf->field = V4L2_FIELD_NONE;
	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		return 0;
	}

	camera->fmt = fmt;

	capturemode = get_capturemode(sd, mf->width, mf->height);
	if (capturemode >= 0) {
		camera->streamcap.capturemode 	= capturemode;
		camera->pix.width 		= mf->width;
		camera->pix.height 		= mf->height;
		return 0;
	}

	dev_err(dev, "[vc-mipi camera] %s: width,height=%d,%d\n", __FUNCTION__, mf->width, mf->height);
	dev_err(dev, "[vc-mipi camera] %s: get_capturemode() failed: capturemode=%d\n", __FUNCTION__, capturemode);
	dev_err(dev, "[vc-mipi camera] %s: Set format failed code=%d, colorspace=%d\n", __FUNCTION__, camera->fmt->code, camera->fmt->colorspace);
	return -EINVAL;
}

int vc_mipi_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct vc_mipi_camera *camera = to_vc_mipi_camera(sd);

	TRACE

	if (fse->index > 0)
        	return -EINVAL;

	fse->max_width = 			camera->sen_pars.frame_dx;
    	fse->min_width = 			camera->sen_pars.frame_dx;
    	fse->max_height = 			camera->sen_pars.frame_dy;
    	fse->min_height = 			camera->sen_pars.frame_dy;

	return 0;
}

int vc_mipi_enum_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_interval_enum *fie)
{
	TRACE
	
	return 0;
}


// *** Initialisation *********************************************************

const struct v4l2_subdev_core_ops vc_mipi_core_ops = {
	.s_power = vc_mipi_s_power,
	.queryctrl = vc_mipi_queryctrl,
	.try_ext_ctrls = vc_mipi_try_ext_ctrls,
	.s_ctrl = vc_mipi_s_ctrl,
	.g_ctrl = vc_mipi_g_ctrl,
};

const struct v4l2_subdev_video_ops vc_mipi_video_ops = {
	.g_frame_interval = vc_mipi_g_frame_interval,
	.s_frame_interval = vc_mipi_s_frame_interval,
	.s_stream = vc_mipi_s_stream,
};

const struct v4l2_subdev_pad_ops vc_mipi_pad_ops = {
	.enum_mbus_code = vc_mipi_enum_mbus_code,
	.get_fmt = vc_mipi_get_fmt,
	.set_fmt = vc_mipi_set_fmt,
	.enum_frame_size = vc_mipi_enum_frame_size,
	.enum_frame_interval = vc_mipi_enum_frame_interval,
};

const struct v4l2_subdev_ops vc_mipi_subdev_ops = {
	.core = &vc_mipi_core_ops,
	.video = &vc_mipi_video_ops,
	.pad = &vc_mipi_pad_ops,
};

// imx8-mipi-csi2.c don't uses the ctrl ops. We have to handle all ops thru the subdev.
const struct v4l2_ctrl_ops vc_mipi_ctrl_ops = {
	// .queryctrl = vc_mipi_queryctrl,
	// .try_ext_ctrls = vc_mipi_try_ext_ctrls,
	// .s_ctrl = vc_mipi_s_ctrl,
	// .g_ctrl = vc_mipi_g_ctrl,
};

struct v4l2_ctrl_config ctrl_config_list[] =
{
{
     	// .ops = &vc_mipi_ctrl_ops,
	.id = V4L2_CID_GAIN,
	.name = "Gain",			// Do not change the name field for the controls!
	.type = V4L2_CTRL_TYPE_INTEGER,
	.flags = V4L2_CTRL_FLAG_SLIDER,
	.min = 0,
	.max = 0xfff,
	.def = 0,
	.step = 1,
},
{
	// .ops = &vc_mipi_ctrl_ops,
	.id = V4L2_CID_EXPOSURE,
	.name = "Exposure",		// Do not change the name field for the controls!
	.type = V4L2_CTRL_TYPE_INTEGER,
	.flags = V4L2_CTRL_FLAG_SLIDER,
	.min = 0,
	.max = 16000000,
	.def = 0,
	.step = 1,
},
};

int vc_mipi_subdev_init(struct v4l2_subdev *sd, struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct v4l2_ctrl_handler* ctrl_hdl;
	struct v4l2_ctrl *ctrl;
	int num_ctrls = ARRAY_SIZE(ctrl_config_list);
    	int ret, i;

	// TRACE

	// Initializes the subdevice
	v4l2_i2c_subdev_init(sd, client, &vc_mipi_subdev_ops);

	// Initializes the ctrls
	ctrl_hdl = devm_kzalloc(dev, sizeof(*ctrl_hdl), GFP_KERNEL);
    	ret = v4l2_ctrl_handler_init(ctrl_hdl, 2);
    	if (ret) {
		dev_err(dev, "[vc-mipi subdev] %s: Failed to init control handler\n",  __FUNCTION__);
		return ret;
	}

	for (i = 0; i < num_ctrls; i++) {
		// ctrl_config_list[i].ops = &vc_mipi_ctrl_ops;
        	ctrl = v4l2_ctrl_new_custom(ctrl_hdl, &ctrl_config_list[i], NULL);
        	if (ctrl == NULL) {
            		dev_err(dev, "[vc-mipi subdev] %s: Failed to init %s ctrl\n",  __FUNCTION__, ctrl_config_list[i].name);
            		continue;
        	}
	}

	if (ctrl_hdl->error) {
		ret = ctrl_hdl->error;
		dev_err(dev, "[vc-mipi subdev] %s: control init failed (%d)\n", __FUNCTION__, ret);
		goto error;
	}

	sd->ctrl_handler = ctrl_hdl;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdl);
	return ret;
}