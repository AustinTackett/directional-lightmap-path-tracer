#pragma once

#include <vector>
#include <algorithm>

#include <GL/glew.h>
#include <glm/glm.hpp>

/*
    Collection of vao, vbo, and ebo
*/

class glVao
{
    private:

    GLuint vao = 0;

    public:

    glVao() {
        glGenVertexArrays(1, &vao);
    }

    ~glVao() {
        if (vao) { glDeleteVertexArrays(1, &vao); }
    }

    GLuint id() {
        return vao;
    }

    bool isCreated() {
        return vao != 0;
    }

    void bind() {
        if (vao) { glBindVertexArray(vao); }
        else     { glBindVertexArray(0); }
    }
};
