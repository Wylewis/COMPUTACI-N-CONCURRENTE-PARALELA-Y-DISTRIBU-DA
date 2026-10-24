#include <cstring>
#include <iostream>
#include <type_traits>
#include <utility>

#include "order.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

bool throwsInvalid(Order& o, OrderState next) {
    try {
        o.transitionTo(next);
    } catch (const InvalidTransition&) {
        return true;
    }
    return false;
}

}

static_assert(!std::is_copy_constructible<Order>::value, "Order no debe ser copiable");
static_assert(!std::is_copy_assignable<Order>::value, "Order no debe ser asignable por copia");
static_assert(std::is_nothrow_move_constructible<Order>::value, "Order debe ser movible");

int main() {

    check(std::strcmp(toString(OrderState::PickedUp), "pickedUp") == 0, "nombre de estado pickedUp");
    check(std::strcmp(toString(RejectReason::QueueFull), "queueFull") == 0, "motivo queueFull");
    check(std::strcmp(toString(RejectReason::Unreachable), "unreachable") == 0, "motivo unreachable");
    check(std::strcmp(toString(RejectReason::NoCourier), "noCourier") == 0, "motivo noCourier");
    check(std::strcmp(toString(EventType::OrderPickedUp), "orderPickedUp") == 0, "evento orderPickedUp");
    check(std::strcmp(toString(EventType::CourierBreakdown), "courierBreakdown") == 0, "evento courierBreakdown");
    check(std::strcmp(toString(EventType::SimulationStopping), "simulationStopping") == 0, "evento simulationStopping");

    {
        Order o;
        o.id = "o1";
        o.assignTo("c3");
        check(o.state == OrderState::Assigned && o.courierId == "c3", "assignTo deja Assigned y guarda el repartidor");
        o.transitionTo(OrderState::PickedUp);
        o.markDelivered(5000);
        check(o.state == OrderState::Delivered && o.deliveredAtMs == 5000, "markDelivered deja Delivered y guarda el tiempo");
        check(isFinal(o.state), "Delivered es final");
        check(throwsInvalid(o, OrderState::Pending), "desde un final no se sale ni a Pending");
    }

    {
        Order o;
        o.id = "o2";
        o.assignTo("c1");
        check(throwsInvalid(o, OrderState::Created), "Assigned no vuelve a Created");
        o.transitionTo(OrderState::PickedUp);
        check(throwsInvalid(o, OrderState::Assigned), "PickedUp no vuelve a Assigned");
        check(!canTransition(OrderState::Created, OrderState::PickedUp), "no se salta Assigned");
        check(!canTransition(OrderState::Created, OrderState::Delivered), "no se salta a Delivered");
    }

    {
        Order o;
        o.id = "o3";
        o.reject(RejectReason::Unreachable);
        check(o.state == OrderState::Rejected && o.rejectReason == RejectReason::Unreachable, "reject guarda el motivo");
        check(!canTransition(OrderState::Assigned, OrderState::Rejected), "un pedido asignado no se rechaza");
        check(!canTransition(OrderState::PickedUp, OrderState::Rejected), "un pedido recogido no se rechaza");
        check(canTransition(OrderState::Created, OrderState::Pending), "Created puede quedar Pending");
        check(canTransition(OrderState::Assigned, OrderState::Pending), "Assigned puede quedar Pending");
        check(canTransition(OrderState::PickedUp, OrderState::Pending), "PickedUp puede quedar Pending");
        check(!canTransition(OrderState::Rejected, OrderState::Pending), "Rejected no pasa a Pending");
    }

    {
        Order a;
        a.id = "o4";
        a.restaurantId = "r2";
        Order b = std::move(a);
        check(b.id == "o4" && b.restaurantId == "r2", "move transfiere los datos");
        check(a.id.empty(), "el origen del move queda vacio");
    }

    if (failures == 0) {
        std::cout << "order_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "order_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
