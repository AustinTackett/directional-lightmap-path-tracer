#pragma once

#include <string>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <iostream>

#include <GL/glew.h>
#include <glm/glm.hpp>

/*
    OpenGL Shader Program Wrapper
*/

class glShader 
{
    private: 

    GLuint shaderProgram = 0;
    std::unordered_map<std::string, GLuint> uniformNameToLocation;

    public: 

    GLuint id()      { return shaderProgram;     }
    bool isCreated() { return shaderProgram != 0; }

    void bind() 
    {         
        if (!shaderProgram) { glUseProgram(0); }
        if (!isBound())     { glUseProgram(shaderProgram); }
    }

    bool isBound()
    { 
        GLint boundProgram; 
        glGetIntegerv(GL_CURRENT_PROGRAM, &boundProgram);
        return boundProgram == static_cast<GLint>(shaderProgram);
    }

    bool loadFromSource(std::string vertexShaderPath_, std::string fragmentShaderPath_)
    {
        GLuint newVertexShader = glCreateShader(GL_VERTEX_SHADER);
        GLuint newFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

        std::string vertexShaderString;
        bool vShaderFileErr = ReadFile(vertexShaderPath_, vertexShaderString);
        if (!vShaderFileErr)
        {
            std::cerr << "Could not load vertex shader at path " << vertexShaderPath_  << std::endl;
            return false;
        }
        std::string fragmentShaderString;
        bool fShaderFileErr = ReadFile(fragmentShaderPath_, fragmentShaderString);
        if (!fShaderFileErr)
        {
            std::cerr << "Could not load fragment shader at path " << fragmentShaderPath_ << std::endl;
            return false;
        }       
        const GLchar* vshaderCharArray = vertexShaderString.c_str();
        const GLchar* fshaderCharArray = fragmentShaderString.c_str();
        glShaderSource(newVertexShader, 1, &vshaderCharArray, NULL);
        glShaderSource(newFragmentShader, 1, &fshaderCharArray, NULL);

        GLint vshaderCompileErr;
        const unsigned int vInfoLogLength = 1024;
        char vShaderInfolog[vInfoLogLength];
        glCompileShader(newVertexShader);
        glGetShaderiv(newVertexShader, GL_COMPILE_STATUS, &vshaderCompileErr);
        if (!vshaderCompileErr) 
        {
            glGetShaderInfoLog(newVertexShader, vInfoLogLength, NULL, &vShaderInfolog[0]);
            glDeleteShader(newVertexShader);
            std::cerr << "Could not compile vertex shaders:" << std::endl << std::string(vShaderInfolog) << std::endl;
            return false;
        }

        GLint fshaderCompileErr;
        const unsigned int fInfoLogLength = 1024;
        char fShaderInfolog[fInfoLogLength];
        glCompileShader(newFragmentShader);
        glGetShaderiv(newFragmentShader, GL_COMPILE_STATUS, &fshaderCompileErr);
        if (!fshaderCompileErr) 
        {
            glGetShaderInfoLog(newFragmentShader, fInfoLogLength, NULL, &fShaderInfolog[0]);
            glDeleteShader(newFragmentShader);
            glDeleteShader(newVertexShader);
            std::cerr << "Could not compile fragment shaders:" << std::endl << std::string(fShaderInfolog) <<std::endl;
            return false;
        }

        GLint linkProgramErr;
        GLuint newShaderProgram = glCreateProgram();
        const unsigned int programInfoLogLength = 1024;
        char programInfolog[programInfoLogLength];
        glAttachShader(newShaderProgram, newVertexShader);
        glAttachShader(newShaderProgram, newFragmentShader);
        glLinkProgram(newShaderProgram);
        glGetProgramiv(newShaderProgram, GL_LINK_STATUS, &linkProgramErr);
        if (!linkProgramErr)
        {
            glGetProgramInfoLog(newShaderProgram, programInfoLogLength, NULL, &programInfolog[0]);
            glDeleteShader(newVertexShader);
            glDeleteShader(newFragmentShader);
            glDeleteProgram(newShaderProgram);
            std::cerr << "Could not link opengl progam" << std::endl << std::string(programInfolog) << std::endl;
            return false;
        }
        glDetachShader(newShaderProgram, newVertexShader);
        glDetachShader(newShaderProgram, newFragmentShader);
        glDeleteShader(newVertexShader);
        glDeleteShader(newFragmentShader);

        if (shaderProgram)
        {
            glDeleteProgram(shaderProgram);
        }

        shaderProgram  = newShaderProgram;
        uniformNameToLocation.clear();
        return true;
    }

    /* 
        Not Exhaustive List of All Potential Uniforms (More Can be Added If Necessary) 
    */

    void setUniformMat4f(std::string name, glm::mat4 data)
    {
        if (!shaderProgram) { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniformMatrix4fv(uniformNameToLocation.at(name), 1, GL_FALSE, &data[0][0]);  
    }

    void setUniformMat4x3f(std::string name, glm::mat4x3 data)
    {
        if (!shaderProgram) { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniformMatrix4x3fv(uniformNameToLocation.at(name), 1, GL_FALSE, &data[0][0]);  
    }

    void setUniformMat4x2f(std::string name, glm::mat4x2 data)
    {
        if (!shaderProgram) { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniformMatrix4x2fv(uniformNameToLocation.at(name), 1, GL_FALSE, &data[0][0]);  
    }

    void setUniformMat3x4f(std::string name, glm::mat3x4 data)
    {
        if (!shaderProgram) { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniformMatrix3x4fv(uniformNameToLocation.at(name), 1, GL_FALSE, &data[0][0]);  
    }

    void setUniformMat3f(std::string name, glm::mat3 data)
    {
        if (!shaderProgram) { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniformMatrix3fv(uniformNameToLocation.at(name), 1, GL_FALSE, &data[0][0]);
    }

    void setUniformMat3x2f(std::string name, glm::mat3x2 data)
    {
        if (!shaderProgram) { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniformMatrix3x2fv(uniformNameToLocation.at(name), 1, GL_FALSE, &data[0][0]);
    }

    void setUniformMat2x4f(std::string name, glm::mat2x4 data)
    {
        if (!shaderProgram) { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniformMatrix2x4fv(uniformNameToLocation.at(name), 1, GL_FALSE, &data[0][0]);  
    }
    
    void setUniformMat2x3f(std::string name, glm::mat2x3 data)
    {
        if (!shaderProgram) { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniformMatrix2x3fv(uniformNameToLocation.at(name), 1, GL_FALSE, &data[0][0]);  
    }

    void setUniformMat2x2f(std::string name, glm::mat2 data)
    {
        if (!shaderProgram) { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniformMatrix2fv(uniformNameToLocation.at(name), 1, GL_FALSE, &data[0][0]);
    }

    void setUniformVec4f(std::string name, glm::vec4 data)
    {
        if (!shaderProgram) { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniform4f(uniformNameToLocation.at(name), data[0], data[1], data[2], data[3]);
    }

    void setUniformVec4fv(std::string name, std::vector<glm::vec4> data, GLsizei size)
    {
        if (!shaderProgram) { return; }
        if (data.empty())   { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniform4fv(uniformNameToLocation.at(name), size, &data[0].x);
    }

    void setUniformVec3f(std::string name, glm::vec3 data)
    {
        if (!shaderProgram) { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniform3f(uniformNameToLocation.at(name), data[0], data[1], data[2]);
    }

    void setUniformVec3fv(std::string name, std::vector<glm::vec3> data, GLsizei size)
    {
        if (!shaderProgram) { return; }
        if (data.empty())   { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniform3fv(uniformNameToLocation.at(name), size, &data[0].x);
    }

    void setUniformVec2f(std::string name, glm::vec2 data)
    {
        if (!shaderProgram) { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniform2f(uniformNameToLocation.at(name), data[0], data[1]);
    }

    void setUniformVec2fv(std::string name, std::vector<glm::vec2> data, GLsizei size)
    {
        if (!shaderProgram) { return; }
        if (data.empty())   { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniform2fv(uniformNameToLocation.at(name), size, &data[0].x);
    }

    void setUniform1f(std::string name, GLfloat data)
    {
        if (!shaderProgram) { return; }

        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniform1f(uniformNameToLocation.at(name), data);
    }

    void setUniform1fv(std::string name, std::vector<GLfloat> data, GLsizei size)
    {
        if (!shaderProgram) { return; }
        if (data.empty())   { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniform1fv(uniformNameToLocation.at(name), size, &data[0]);
    }

    // Mostly used for setting texture units
    void setUniform1i(std::string name, GLint data)
    {
        if (!shaderProgram) { return; }

        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) { 
            GLint location = glGetUniformLocation(shaderProgram, name.c_str()); 
            if (location < 0) { return; }   
            uniformNameToLocation[name] = location; 
        }
        glUniform1i(uniformNameToLocation.at(name), data);
    }

    void setUniform1ui(std::string name, GLuint data)
    {
        if (!shaderProgram) { return; }
        if (uniformNameToLocation.find(name) == uniformNameToLocation.end()) {
            GLint location = glGetUniformLocation(shaderProgram, name.c_str());
            if (location < 0) { return; }
            uniformNameToLocation[name] = location;
        }
        glUniform1ui(uniformNameToLocation.at(name), data);
    }

    bool ReadFile(std::string filePath, std::string& outString)
    {
        std::ifstream file(filePath);    
        if (!file.is_open())
        {
            return false;
        }
        std::string tempStringLine; 
        outString.clear();
        while(std::getline(file, tempStringLine))
        {
            outString += tempStringLine;
            outString += '\n';
        }
        return true; 
    }
};
