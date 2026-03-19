#ifndef STATEMENT_HPP
#define STATEMENT_HPP
#include <stdexcept>

class Statement{
    public:
        virtual void execute() {
            throw std::runtime_error("Direct execution not supported. Use Engine::execute() instead.");
        }
        virtual ~Statement() = default;
};

#endif