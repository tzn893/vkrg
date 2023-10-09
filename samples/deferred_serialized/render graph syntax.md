Render graph是一个用json文件描述有向无环图。其包含以下属性。

*note 0.1  当下述参数未出现在json文件时取默认值, 字符串类型为”“，数字类型为0, 数组类型为空数组[]*

#### 1. prototypes

prototypes 类型为 数组。包含prototype元素。

prototype为render graph中节点的原型，render graph中每个节点均对应一个prototype。用户可以通过在C++代码中继承类ExecutablePass, 来创建新prototype。

```c++
class DeferredPass : public ExecutablePass
{
public:
	DeferredPass(const std::string& name);

	...
};
```

一个prototype包含以下属性

##### 1.1 prototype.prototype

prototype.prototype 类型为 字符串

该prototype的名字，和在c++代码中对应类名相同

*rule 1.1.1 同一个prototype数组中的 prototype.prototype 不允许重复*

##### 1.2 prototype.type

prototype.type 类型为 字符串

该prototype的类型，对应c++中 `VKRG_RENDER_PASS_TYPE` 枚举类，有效值有:

```c++
"compute-pass" == VKRG_RP_TYPE_COMPUTE_PASS
"render-pass" == VKRG_RP_TYPE_RENDER_PASS
"resource-input" == VKRG_RP_TYPE_RESOURCE_OUTPUT
"resource-output" == VKRG_RP_TYPE_RESOURCE_INPUT
"resource-in-out" == VKRG_RP_TYPE_RESOURCE_IN_OUT
```

*rule 1.2.1 `prototype.type` 的值不能是上述值之外的值*

##### 1.3 prototype.input

prototype.input 类型为数组，包含元素input

该prototype的输入参数input，其属性包含

###### 	1.3.1 prototype.input.name

​	prototype.input.name 类型为字符串

​	输入参数的名字

​	*rule 1.3.1.1 同一个prototype.input数组中的prototype.input.name值不允许重复*

###### 	1.3.2 prototype.input.layout

​	prototype.input.layout 类型为字符串

​	该输入参数的layout类型，对应c++中`VKRG_LAYOUT` 枚举类,有效值有:

```c++
"texture1d" == VKRG_LAYOUT_TEXTURE1D,
"texture2d" == VKRG_LAYOUT_TEXTURE2D,
"texture3d" == VKRG_LAYOUT_TEXTURE3D,
"buffer" == VKRG_LAYOUT_BUFFER
```

*rule 1.3.2.1 `prototype.input.layout` 的值不能是上述值之外的值*	

###### 1.3.3  prototype.input.format

​	prototype.input.format 类型为字符串

​	该输入参数的格式，对应c++中`VKRG_FORMAT`类，有效值有:

```c++
"buffer" = VKRG_FORMAT_BUFFER,
"rgba8" = VKRG_FORMAT_RGBA8,
"d24s8" = VKRG_FORMAT_D24S8
```

 rule 1.3.3.1: `prototype.input.format` 的值不能是上述值之外的值

 rule 1.3.3.2: 当`prototype.input.layout == "buffer"` 时 `prototype.input.format`必须为 `"d24s8"`

 rule 1.3.3.3: 只有当`prototype.input.layout == "texture2d"` 时 `prototype.input.format`的值才能为`"d24s8"`

###### 1.3.4 prototype.input.channel-count

prototype.input.channel-count 类型为 正整数

该输入参数的通道数。对于texture array，该参数为array的长度。对于texture cube, 该参数为6。

note 1.3.4.1: 当`prototype.type=="resource-input" | "resource-output" | "resource-in-out"` 时，该参数会被无视。

1.3.6 prototype.input.mip-idx

1.3.6 prototype.input.mip-idx

1.3.6 prototype.input.mip-idx

##### 1.4 prototype.output

该prototype的输出参数，其属性包含

###### 	1.4.1 prototype.output.name

​	同1.3.1

###### 	1.4.2 prototype.output.layout

​	同1.3.2

###### 	1.4.3  prototype.output.format

​	同1.3.3

###### 	1.4.4 prototype.output.extent

​	描述了输出参数长宽，方便render graph自动创建临时变量。

​	其包含以下属性

###### 		1.4.4.1 prototype.output.extent.size

​		prototype.output.extent.size 类型为 非负整数

​		描述了当参数为buffer时其大小。

​		*rule 1.4.4.1.1 当 `prototype.output.layout == "buffer"` 时，size值必须为正整数*

​		*note 1.4.4.1.1 当 `prototype.output.layout != "buffer"`时，该值会被忽视*

######     	1.4.4.2 prototype.output.extent.width

​		prototype.output.extent.width 类型为 非负整数

​		描述了参数的宽度

​		*rule 1.4.4.2.1 当`prototype.output.layout == "texture1d" | "texture3d" ` 时prototype.output.extent.width 值必须为正整数*

​		rule 1.4.4.2.2 当`prototype.output.layout == "texture2d" ` 且`prototype.output.extent.screen-scale == 0` 时 prototype.output.extent.width 值必须为正整数

​		*note 1.4.4.2.1 当`prototype.output.layout == "buffer"` 时，该值会被无视*

###### 		1.4.4.3 prototype.output.extent.height

​		prototype.output.extent.height 类型为 非负整数

​		描述了参数的长度

​		*rule 1.4.4.3.1 当`prototype.output.layout ==  "texture2d" | "texture3d" ` 时prototype.output.extent.width 值必须为正整数*

​		rule 1.4.4.3.2 当`prototype.output.layout == "texture2d" ` 且`prototype.output.extent.screen-scale == 0` 时 prototype.output.extent.height 值必须为正整数

​		*note 1.4.4.3.1 当`prototype.output.layout == "texture1d" |"buffer"` 时，该值会被无视*

###### 		1.4.4.4 prototype.output.extent.depth

​		prototype.output.extent.depth 类型为非负整数

​		描述了参数的深度(通常用于3d texture)

​		*rule 1.4.4.4.1 当`prototype.output.layout == "texture3d" ` 时prototype.output.extent.width 值必须为正整数*

​		*note 1.4.4.4.1 当`prototype.output.layout == "texture2d" |"texture1d" |"buffer"` 时，该值会被无视*

###### 		1.4.4.5 prototype.output.extent.screen-scale

​		prototype.output.extent.screen-scale 类型为非负浮点数

​		描述了参数随屏幕缩放比例（例如screen-scale = 1.5 的texture2d, 在屏幕分辨率为1000x600时长宽为 1500x900）

​		note 1.4.4.5.1 当`prototype.output.layout == "texture3d" | "texture1d" |"buffer"  `  该值将被无视

​		rule 1.4.4.5.1 当`prototype.output.layout == "texture2d` 且`prototype.output.layout.width == 0` 或`prototype.output.layout.height == 0` 时，该值必须为正数。

###### 	1.4.5 prototype.input.channel-count

​	同1.3.4

#### 2. resources

​	resources 类型为数组，描述了从render graph引入的资源