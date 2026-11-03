#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

#include <vector>
#include <algorithm>

/*
    Collection of vao, vbo, and ebo
*/

class glVertexObject
{
    private:

    GLuint vao = 0;

    std::vector<GLuint>  vbos;
    std::vector<GLsizei> vboCounts;

    GLuint  ebo = 0;
    GLsizei eboCount = 0;

    public:

    glVertexObject() {
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glBindVertexArray(0);
    }

    ~glVertexObject() {
        if (!vbos.empty()) { glDeleteBuffers(static_cast<GLsizei>(vbos.size()), vbos.data()); }
        if (ebo)           { glDeleteBuffers(1, &ebo); }
        if (vao)           { glDeleteVertexArrays(1, &vao); }
    }

    GLuint id() {
        return vao;
    }

    bool isCreated() {
        return vao != 0;
    }

    GLsizei indexCount() {
        return eboCount;
    }

    bool hasIndices() {
        return eboCount > 0;
    }

    void bind() {
        if (vao) { glBindVertexArray(vao); }
        else     { glBindVertexArray(0); }
    }

    void draw( GLsizei count = -1, GLsizei offset = 0, GLenum mode = GL_TRIANGLES, bool useIndices = true)
    {
        if (!vao) { return; }

        bind();

        // Draw Elements
        if (useIndices && hasIndices())
        {
            if (offset < 0 || offset >= eboCount) { return; }

            GLsizei drawCount = (count < 0) ? (eboCount - offset) : count;
            if (drawCount <= 0) { return; }
            glDrawElements(
                mode,
                drawCount,
                GL_UNSIGNED_INT,
                reinterpret_cast<const void*>(static_cast<size_t>(offset) * sizeof(unsigned int))
            );
            return;
        }

        // Draw Arrays
        GLsizei vboCount = *std::min_element(vboCounts.begin(), vboCounts.end());
        if (vboCount <= 0 || offset < 0 || offset >= vboCount) { return; }

        GLsizei drawCount = (count < 0) ? (vboCount - offset) : count;
        if (drawCount <= 0) { return; }

        glDrawArrays(mode, offset, drawCount);
    }

    template<typename T>
    void addVertices(GLuint location, std::vector<T>& data,  GLint dimension, 
        GLenum dataType = GL_FLOAT, GLenum usage = GL_STATIC_DRAW)
    {
        if (!vao)         { return; }
        if (data.empty()) { return; }

        glBindVertexArray(vao);

        GLuint vbo = 0;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(data.size()) * static_cast<GLsizeiptr>(sizeof(T)),
            data.data(),
            usage);

        glVertexAttribPointer(location, 
            dimension, 
            dataType, 
            GL_FALSE, 
            sizeof(T), 
            nullptr);
        glEnableVertexAttribArray(location);

        vbos.push_back(vbo);
        vboCounts.push_back(static_cast<GLsizei>(data.size()));

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    void setIndices(std::vector<unsigned int>& indices, GLenum usage = GL_STATIC_DRAW)
    {
        if (!vao)            { return; }
        if (indices.empty()) { return; }
        if (ebo)             { glDeleteBuffers(1, &ebo); }


        glBindVertexArray(vao);

        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(indices.size()) * static_cast<GLsizeiptr>(sizeof(unsigned int)),
            indices.data(),
            usage);

        eboCount = static_cast<GLsizei>(indices.size());

        glBindVertexArray(0);
    }
};
