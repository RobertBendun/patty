# Patty - LISP with patterns

Created for [Lang Jam](https://github.com/langjam/jam0002)

Language inspired by one line of Haskell code:

```haskell
fibs = 0 : 1 : zipWith (+) fibs (tail fibs)
```

### Examples

| Patty | Haskell |
| ----- | ------- |
| `(seq 5)` | `repeat 5` |
| `(seq 0 1)` | `cycle [0, 1]` |
| `(def repeat (fn (v) (seq! v)))` | `repeat n = n : repeat n` |
| `(zip-with + (list 1 2 3) (list 10 20 30))` | `zipWith (+) [1,2,3] [10,20,30]` |
| `(fold * (take 5 (seq (+ n 1))))` | `foldl1 (*) $ take 5 $ [1..]` |

See [examples](examples/)
