#version 430 core
#define RENDER_3D	1

#if RENDER_3D==0
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec2 aTexCoord;
out vec3 ourColor;
#endif

#if RENDER_3D==1
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
#endif

out vec2 TexCoord;

uniform mat4 transform;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
	gl_Position = projection * view * model * transform * vec4(aPos, 1.0);
	#if RENDER_3D==0
	ourColor = aColor;
	#endif
	TexCoord = vec2(aTexCoord.x, aTexCoord.y);
}