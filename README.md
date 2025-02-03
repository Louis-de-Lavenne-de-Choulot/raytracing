# raytracing
 following gabriel gambetta's fun book on computer graphics

# compiling

```ps
docker build -t gcc-comp .
docker run -v "$(pwd):/codehere" -it --name gcc_comp gcc_compiler
#if restart :
docker start --name gcc_comp
docker exec -it gcc_comp bash

#compile
make all

#cleanup
make clean
```