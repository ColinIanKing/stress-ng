#include <stdio.h>
#include <stdlib.h>
#include <GLES2/gl2.h>

int main(void)
{
	const uint32_t size_x = 16, size_y = 16;
    const int frag_n = 0;
    GLuint program;
    static GLubyte *teximage = NULL;
    GLsizei texsize = 1024;
    GLsizei uploads = 1;

	GLint ufrag_n = glGetUniformLocation(program, "frag_n");
	glUniform1i(ufrag_n, frag_n);
	if(glGetError() != GL_NO_ERROR)
		printf("Failed to get the storage location of %d\n", frag_n);


	if (texsize > 0) {
		GLint maxsize;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxsize);
		if(texsize > maxsize)
			printf("Image size exceeds max texture size \n");

		GLuint texobj = 0;
		glGenTextures(1, &texobj);
		glBindTexture(GL_TEXTURE_2D, texobj);

		GLint bytesPerImage = texsize * texsize * 4;
		teximage = malloc(bytesPerImage);
        for (int i = 0; i < uploads; i++) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texsize, texsize,
					0, GL_RGBA, GL_UNSIGNED_BYTE, teximage);
		}
	}
    return 0;
}