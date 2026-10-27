# ZJUADS Project 2: Dijkstra's Algorithm with Heap Structures

Shortest path problems are one of the most fundamental combinatorial optimization problems with many applications, both direct and as subroutines in other combinatorial optimization algorithms. Algorithms for these problems have been studied since 1950's and still remain an active area of research.

In this project you are supposed to compute the shortest paths using Dijkstra's algorithm. The implementation shall be based on a min-priority queue, such as a Fibonacci heap. The goal of the project is to find the best data structure for the Dijkstra's algorithm.

## Your tasks are

1. Implement the algorithm with at least two different heap structures, while Fibonacci heap must be one of them.
2. Use the USA road networks for evaluation. The data sets can be downloaded from <http://www.dis.uniroma1.it/challenge9/download.shtml> which provides the benchmarks for the 9th DIMACS Implementation Challenge. (Note: you must only list the download links of these test data sets instead of uploading them with your reports.)
3. At least 1000 pairs of query are required in evaluating the run times of the algorithm with various of implementations.

## Grading Policy

**Programming:** Implement Dijkstra's algorithm with Fibonacci heap and other heaps (6 pts.). Write a test of performance program (3 pts.). All the codes must be sufficiently commented.

**Testing:** Provide the necessary inputs for testing and give the run time table (2 pts.). Plot the run times vs. input sizes for illustration (2 pts.). Write analysis and comments (3 pts.).

**Documentation:** Chapter 1 (1 pt.), Chapter 2 (2 pts.), and finally a complete report (1 point for overall style of documentation).
