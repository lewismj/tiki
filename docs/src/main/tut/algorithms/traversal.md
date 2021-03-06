---
layout: docs 
title:  "Traversal"
section: "algorithms"
source: "core/src/main/scala/tiki/Traversal.scala"
scaladoc: "#tiki.Traversal"
---
# Traversal Functions
 
Traversal is done by performing an `unfold` on the graph representation, the 
 function will return a stream of vertices.
```scala
  def unfold[T,R](z: T)(f: T => Option[(R,T)]): Trampoline[Stream[R]] = f(z) match {
    case None => Trampoline.done(Stream.empty[R])
    case Some((r,v)) => Trampoline.suspend(unfold(v)(f)).flatMap(s => Trampoline.done(r #:: s))
  }
```
The traversal can be depth or breadth first. _Note_ the `distinct` on the stream (_visitOrder_ function) 
does preserve order. 
A vertex may be visited more than once in a traversal, most of the time we want the first instance.

Currently, cycles are ignored (i.e. the stream _won't_ loop infinitely.)
```scala
  private def traverse[A](g: Directed[A], l: Stream[A])(implicit ev: Semigroup[Stream[A]]): Stream[A]
  = unfold( (l,Stream.empty[A]) ) {
    case (current,visited) => current match {
      case w #:: vs => Some((w, (ev.combine(g.successors(w),vs).diff(visited), visited #::: Stream(w))))
      case _ => None
    }
  }.run
= unfold( (l,Set.empty[A]) ) {
      case (current,visited) => current match {
        case w :: Nil =>
          Some((w, (g.successors(w).toList.filterNot(visited.contains), visited + w)))
        case w :: vs =>
          val next = if (dfs) g.successors(w).toList ::: vs
          else vs ::: g.successors(w).toList
          Some((w, (next.filterNot(visited.contains), visited + w)))
        case _ =>
          None
      }
    }.run
```
 Two primary functions are available:
 
 - `dfs(g,start)` will perform a depth-first traversal of the graph _g_, starting at _start_ vertex.
 - `bfs(g,start)` as above, except a breadth first search will be done.
 
 Both are implemented in terms of the `visitOrder` function.
 
 ```scala
  private def visit[A](g: Directed[A], start: A)(implicit ev: Semigroup[Stream[A]]): Stream[A]
    = if (g.contains(start)) traverse(g,Stream(start))(ev).distinct else Stream.empty[A]
  def dfs[A](g: Directed[A], start: A): Stream[A] = visit(g,start)((x: Stream[A], y: Stream[A]) => x #::: y)
  def bfs[A](g: Directed[A], start: A): Stream[A] = visit(g,start)((x: Stream[A], y: Stream[A]) => y #::: x)
```

### Depth first search

![graph](https://raw.github.com/lewismj/tiki/master/docs/src/main/resources/microsite/img/dfs.png)

```tut
import tiki._
import tiki.Traversal._
import tiki.implicits._


val edges = Stream('a' --> 'b', 'a' --> 'c', 'b' --> 'd', 'c' --> 'd')
val adj = AdjacencyList(edges)
val dfsSearch = dfs(adj, 'a')
dfsSearch.mkString
```


### Breadth first search

![graph](https://raw.github.com/lewismj/tiki/master/docs/src/main/resources/microsite/img/bfs.png)

```tut
import tiki._
import tiki.Traversal._
import tiki.implicits._


val edges = Stream('a' --> 'b', 'a' --> 'c', 'b' --> 'd', 'c' --> 'd')
val adj = AdjacencyList(edges)
val bfsSearch = bfs(adj, 'a')
bfsSearch.mkString
```
