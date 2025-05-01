import DefaultBackend
import Observation
import SwiftCrossUI

struct Person {
    let name: String
    let age: Int
}

class Counter: ObservableObject {
    var count: Int = 0

    func update(by amount: Int) {
        let newCount = count + amount
        // if newCount >= 0 {
        count = newCount
        // }
    }
}

protocol SanitizationState {}

enum Raw: SanitizationState {}
enum Sanitized: SanitizationState {}

struct FormData<S: SanitizationState> {

    let value: String

    static func create(with data: String) -> FormData<Raw> {
        .init(data)
    }

    private let _state: (Never) -> S

    private init(_ data: String) {
        self.value = data
        self._state = { _ in fatalError("S should never be used") }
    }
}

func read(with data: String) -> FormData<Raw> {
    print("read")
    return .create(with: data)
}

extension FormData where S == Raw {
    consuming func sanitize() -> FormData<Sanitized> {
        print("sanitized")
        return FormData<Sanitized>.init(value)
    }
}

func run(query: FormData<Sanitized>) {
    print("ran \(query.value)")
}

func solve<T: Hashable>(_ array: [T]) -> [T] {
    let threshold = array.count / 3
    let elements = Dictionary(grouping: array, by: { $0 }).filter { $1.count > threshold }
    return Array(elements.keys)
}

@main
struct App {
    static func main() {
        print(solve([3, 2, 3]))
        print(solve([1]))
        print(solve([1, 2]))
    }
}

// @main
// struct CounterApp: App {
//     @Published private var counter = Counter()
//     //@State var people = [Person(name: "Rafael", age: 32)]

//     var body: some Scene {
//         WindowGroup("CounterApp") {
//             HStack {
//                 Button("-") { counter.update(by: -1) }
//                 Text("Count: \(counter.count)")
//                 Button("+") { counter.update(by: 1) }
//                 // VStack {
//                 //     ForEach(people) { p in
//                 //         Text("name = \(p.name)")
//                 //     }
//                 // }
//             }.padding()
//         }
//     }
// }
