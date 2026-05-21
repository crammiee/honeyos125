Operating Systems Concepts
(CMSC 125 Project)
Phase 2: (Learning From MIMIX.3)
Study how mimix.3 is implemented and incorporate your knowledge into Honey OS.

Requirements:
1. Make Honey OS runnable of its own, not on top of an existing OS. It should
be run in a virtual machine.
2. Your OS will be run as infinite loop. While it is running, it checks for some
tasks to do until you shut it down.
3. Start by running the main.c program, which is found in line number 07100.
To understand how it works, implement the first couple of fucntions. Then
commnent out the other function calls so that so that you can focus on
understanding how to boot an OS. Later uncomment the other function calls
as you progress.
4. By the end of the semester, your Honey OS will have a welcome screen and
the capability to do basic file operations like read, write, edit, and delete
files and create, change, delete, and list files in a given directory as the
minimum requirement. Of course, it must include the boot and shutdown
operations. Other operations may also be implemented to make Honey OS
equipped with better functionalities.
5. Focus your work on implementing the file allocation method (linked or
indexed). The block size may be 32, 64, 128, 256, or 512 KB. It should be
powers of 2. Create your simpe FAT.
6. Make sure to perform a good programming practice. What I mean is to
heavily comment your code. For each function you called, make a comment
that will be easy for you to remember and for others to easily understand
your code. This a part of the code documentation.

Suggestions:
1. A member of the team should study on how to set up a particular
virtual machine and use it.
2. The other team members will be working on the development of the
Honey OS.
3. Keep discussing among yourselves to come up with a better output.

Points:
70% correctness (group) – according to the specifications, this includes heavy
comments
10% aesthetics (group) – layout and design
10% creativity (group) – introduced something useful
10% honest peer-evaluation (individual) – rate of member’s contributions