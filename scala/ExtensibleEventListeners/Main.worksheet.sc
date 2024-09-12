import scala.reflect._

case class TypeMap(entries: Map[Class[?], Any] = Map.empty) {
  def constains[T: ClassTag]: Boolean =
    entries.contains(TypeMap.keyOf[T])

  def put[T: ClassTag](value: T): TypeMap =
    TypeMap(entries + (TypeMap.keyOf[T] -> value))

  def get[T: ClassTag]: Option[T] =
    entries.get(TypeMap.keyOf[T]).asInstanceOf[Option[T]]
}

object TypeMap {
  def keyOf[T: ClassTag]: Class[?] = classTag[T].runtimeClass
}

case class Dispatcher(eventToListeners: TypeMap = TypeMap()) {
  type Listener[E] = E => Unit
  type ListenerList[E] = Seq[Listener[E]]

  def registerOn[E: ClassTag](listener: Listener[E]): Dispatcher =
    Dispatcher(
      eventToListeners.put[ListenerList[E]](
        listener +: eventToListeners
          .get[ListenerList[E]]
          .getOrElse(Seq())
      )
    )

  def trigger[E: ClassTag](event: E): Unit =
    eventToListeners
      .get[ListenerList[E]]
      .foreach(_.foreach(_(event)))
}

case class OnClick(mouseX: Int, mouseY: Int)

val dispatcher = Dispatcher()
  .registerOn((ev: OnClick) =>
    println(s"1: (x, y): (${ev.mouseX}, ${ev.mouseY})")
  )
  .registerOn((ev: OnClick) =>
    println(s"2: (x, y): (${ev.mouseX}, ${ev.mouseY})")
  )

dispatcher.trigger(OnClick(400, 600))
