import $ivy.`org.typelevel::cats-core:2.3.0`

import cats.implicits._

/** A handmade, poorly written Reader.
  *
  * It interacts rather badly with type-inference due to lacking variance
  * annotations, etc.
  */
class Reader[R, A](val runWith: R => A) {
  def map[B](f: A => B): Reader[R, B] = this.flatMap(f andThen Reader.pure)

  def flatMap[B](k: A => Reader[R, B]): Reader[R, B] =
    Reader(r => k(this.runWith(r)).runWith(r))

  def local(f: R => R): Reader[R, A] = Reader.asks(f andThen this.runWith)
}

object Reader {
  def apply[R, A](run: R => A): Reader[R, A] = new Reader(run)

  def pure[R, A](a: A): Reader[R, A] = Reader(Function.const(a))

  def ask[R]: Reader[R, R] = Reader(identity)

  def asks[R, A](f: R => A): Reader[R, A] = Reader.ask.map(f)
}

sealed trait Expr

object Expr {
  case class Lit(i: Int) extends Expr
  case class Bool(b: Boolean) extends Expr
  case class Var(n: Ident) extends Expr
  case class Let(n: Ident, h: Expr, b: Expr) extends Expr
  case class Add(l: Expr, r: Expr) extends Expr
  case class Cond(c: Expr, t: Expr, e: Expr) extends Expr
}

sealed trait Value

object Value {
  case class Lit(i: Int) extends Value
  case class Bool(b: Boolean) extends Value

  def add(l: Value, r: Value): Option[Value] = (l, r) match {
    case (Value.Lit(l), Value.Lit(r)) => Value.Lit(l + r).some
    case _                            => none
  }
}

type Ident = String

type Env = Map[Ident, Value]
val Env = Map

type Eval = Reader[Env, Option[Value]]
val Eval = Reader

implicit class ExprEval(expr: Expr) {

  def eval: Eval = expr match {
    case Expr.Lit(i)  => liftLit(i)
    case Expr.Bool(b) => liftBool(b)
    case Expr.Var(n)  => Eval.asks(_.get(n))
    case Expr.Let(n, h, b) =>
      h.eval.flatMap {
        _.map(vh => b.eval.local(_ + (n -> vh))).getOrElse(fail)
      }
    case Expr.Add(l, r) =>
      for {
        vl <- l.eval
        vr <- r.eval
      } yield (vl, vr).mapN(Value.add).flatten
    case Expr.Cond(c, t, e) =>
      c.eval
        .flatMap {
          _.collect {
            case Value.Bool(true)  => t
            case Value.Bool(false) => e
          }.map(_.eval).getOrElse(fail)
        }
  }
}

def liftLit(i: Int): Eval = litValue(Value.Lit(i))
def liftBool(b: Boolean): Eval = litValue(Value.Bool(b))
def litValue(v: Value): Eval = Eval.pure(v.some)
def fail: Eval = Eval.pure(none)

val expr = Expr.Let(
  "y",
  Expr.Add(Expr.Lit(1), Expr.Lit(2)),
  Expr.Cond(
    Expr.Var("x"),
    Expr.Add(Expr.Lit(10), Expr.Lit(20)),
    Expr.Bool(false)
  )
)

val env = Env("x" -> Value.Bool(true))

expr.eval.runWith(env)
