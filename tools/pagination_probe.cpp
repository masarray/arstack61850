// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ar::iec61850;

struct PageEvidence final {
    std::size_t page{};
    std::string request_continue_after;
    std::size_t response_name_count{};
    std::string response_last_name;
    bool more_follows{};
};

struct QueryEvidence final {
    mms::MmsGetNameListObjectClass object_class{mms::MmsGetNameListObjectClass::domain};
    std::string domain;
    std::vector<PageEvidence> pages;
    std::vector<std::string> names;

    [[nodiscard]] bool paginated() const noexcept {
        return pages.size() > 1U || std::any_of(
            pages.begin(), pages.end(), [](const auto& page) { return page.more_follows; });
    }

    [[nodiscard]] std::size_t continuation_request_count() const noexcept {
        return static_cast<std::size_t>(std::count_if(
            pages.begin(), pages.end(), [](const auto& page) {
                return !page.request_continue_after.empty();
            }));
    }
};

[[nodiscard]] std::string_view class_name(
    const mms::MmsGetNameListObjectClass object_class) noexcept {
    switch (object_class) {
    case mms::MmsGetNameListObjectClass::domain:
        return "Domain";
    case mms::MmsGetNameListObjectClass::named_variable:
        return "NamedVariable";
    case mms::MmsGetNameListObjectClass::named_variable_list:
        return "NamedVariableList";
    }
    return "Unknown";
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::ostringstream output;
    for (const char character : value) {
        switch (character) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) output << '?';
            else output << character;
            break;
        }
    }
    return output.str();
}

[[nodiscard]] std::size_t parse_positive(
    const std::string& option,
    const std::string& value) {
    std::size_t consumed = 0U;
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0ULL ||
        parsed > static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(option + " requires a positive bounded integer.");
    }
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] std::uint16_t parse_port(const std::string& value) {
    const auto parsed = parse_positive("port", value);
    if (parsed > 65'535U) {
        throw std::invalid_argument("port must be in the range 1..65535.");
    }
    return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] std::span<const std::uint8_t> response_payload(
    const mms::MmsConfirmedExchangeResult& exchange) {
    if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
        throw std::runtime_error("GetNameList did not return a confirmed response.");
    }
    if (!exchange.presentation_payload.empty()) {
        return exchange.presentation_payload;
    }
    if (!exchange.envelope.mms_payload.empty()) {
        return exchange.envelope.mms_payload;
    }
    throw std::runtime_error("GetNameList returned no decodable response payload.");
}

[[nodiscard]] QueryEvidence run_query(
    mms::MmsAssociationRuntime& association,
    const mms::MmsGetNameListObjectClass object_class,
    const std::string& domain,
    const std::size_t maximum_pages,
    const std::size_t maximum_names) {
    QueryEvidence evidence;
    evidence.object_class = object_class;
    evidence.domain = domain;

    std::set<std::string, std::less<>> seen;
    std::string continue_after;

    for (std::size_t page_index = 0U; page_index < maximum_pages; ++page_index) {
        mms::MmsGetNameListRequest request;
        request.invoke_id = association.next_invoke_id();
        request.object_class = object_class;
        request.scope = domain.empty()
            ? mms::MmsObjectScopeKind::vmd_specific
            : mms::MmsObjectScopeKind::domain_specific;
        request.domain_id = domain;
        request.continue_after = continue_after;

        const auto encoded = mms::MmsServiceCodec::encode_get_name_list_request_p_data(
            request, association.negotiated().presentation_context_id);
        const auto exchange = association.exchange_confirmed(encoded, request.invoke_id);
        const auto response = mms::MmsServiceCodec::decode_get_name_list_response(
            response_payload(exchange), request.invoke_id);

        PageEvidence page;
        page.page = page_index + 1U;
        page.request_continue_after = request.continue_after;
        page.response_name_count = response.names.size();
        page.more_follows = response.more_follows;
        if (!response.names.empty()) {
            page.response_last_name = response.names.back();
        }
        evidence.pages.push_back(page);

        const auto count_before = evidence.names.size();
        for (const auto& name : response.names) {
            if (!seen.insert(name).second) {
                continue;
            }
            if (evidence.names.size() >= maximum_names) {
                throw std::runtime_error(
                    std::string{class_name(object_class)} + " name bound exceeded.");
            }
            evidence.names.push_back(name);
        }

        if (!response.more_follows) {
            return evidence;
        }
        if (response.names.empty() || evidence.names.size() == count_before) {
            throw std::runtime_error(
                std::string{class_name(object_class)} +
                " reported moreFollows without forward progress.");
        }

        continue_after = response.names.back();
    }

    throw std::runtime_error(
        std::string{class_name(object_class)} + " exceeded the page bound.");
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_pagination_probe <host> [port] [options]\n"
        << "Options:\n"
        << "  --timeout-ms N        Connect/request timeout (default 30000).\n"
        << "  --max-pages N         Maximum pages per GetNameList query (default 256).\n"
        << "  --max-names N         Maximum unique names per query (default 65536).\n"
        << "  --max-domains N       Maximum domains to inspect (default 4096).\n"
        << "  --require-pagination  Return failure unless real continuation is observed.\n"
        << "  --json                Emit machine-readable evidence JSON.\n"
        << "\nThis probe sends GetNameList only; it never sends MMS Write or mutation requests.\n";
}

void print_human(
    const mms::MmsEndpoint& endpoint,
    const mms::MmsAssociationRuntime& association,
    const std::vector<QueryEvidence>& queries,
    const bool accepted) {
    const auto paginated = static_cast<std::size_t>(std::count_if(
        queries.begin(), queries.end(), [](const auto& query) { return query.paginated(); }));
    std::size_t continuations = 0U;
    for (const auto& query : queries) {
        continuations += query.continuation_request_count();
    }

    std::cout << "Read-only GetNameList pagination probe: endpoint="
              << endpoint.host << ':' << endpoint.port
              << ", associationProfile="
              << (association.active_association_profile().empty()
                      ? std::string{"unknown"}
                      : association.active_association_profile())
              << ", associationAttempts=" << association.association_attempts().size()
              << ", queries=" << queries.size()
              << ", paginatedQueries=" << paginated
              << ", continuationRequests=" << continuations
              << ", accepted=" << (accepted ? "true" : "false") << ".\n";

    for (const auto& query : queries) {
        std::cout << "Query " << class_name(query.object_class)
                  << " scope=" << (query.domain.empty() ? "VMD" : query.domain)
                  << " pages=" << query.pages.size()
                  << " names=" << query.names.size()
                  << " continuations=" << query.continuation_request_count()
                  << ".\n";
        for (const auto& page : query.pages) {
            std::cout << "  page=" << page.page
                      << " requestContinueAfter="
                      << (page.request_continue_after.empty()
                              ? std::string{"-"}
                              : page.request_continue_after)
                      << " responseNames=" << page.response_name_count
                      << " responseLastName="
                      << (page.response_last_name.empty()
                              ? std::string{"-"}
                              : page.response_last_name)
                      << " moreFollows=" << (page.more_follows ? "true" : "false")
                      << '\n';
        }
    }
}

void print_json(
    const mms::MmsEndpoint& endpoint,
    const mms::MmsAssociationRuntime& association,
    const std::vector<QueryEvidence>& queries,
    const bool accepted) {
    const auto paginated = static_cast<std::size_t>(std::count_if(
        queries.begin(), queries.end(), [](const auto& query) { return query.paginated(); }));
    std::size_t continuations = 0U;
    for (const auto& query : queries) {
        continuations += query.continuation_request_count();
    }

    std::cout << '{'
              << "\"schemaVersion\":\"ariec61850-pagination-evidence-v1\","
              << "\"endpoint\":\"" << json_escape(endpoint.host) << ':'
              << endpoint.port << "\","
              << "\"readOnly\":true,"
              << "\"associationProfile\":\""
              << json_escape(association.active_association_profile()) << "\","
              << "\"associationAttempts\":" << association.association_attempts().size() << ','
              << "\"queryCount\":" << queries.size() << ','
              << "\"paginatedQueryCount\":" << paginated << ','
              << "\"continuationRequestCount\":" << continuations << ','
              << "\"accepted\":" << (accepted ? "true" : "false") << ','
              << "\"queries\":[";

    for (std::size_t query_index = 0U; query_index < queries.size(); ++query_index) {
        if (query_index != 0U) std::cout << ',';
        const auto& query = queries[query_index];
        std::cout << '{'
                  << "\"objectClass\":\"" << class_name(query.object_class) << "\","
                  << "\"scope\":\""
                  << json_escape(query.domain.empty() ? std::string{"VMD"} : query.domain)
                  << "\","
                  << "\"pageCount\":" << query.pages.size() << ','
                  << "\"uniqueNameCount\":" << query.names.size() << ','
                  << "\"continuationRequestCount\":"
                  << query.continuation_request_count() << ','
                  << "\"paginated\":" << (query.paginated() ? "true" : "false") << ','
                  << "\"pages\":[";
        for (std::size_t page_index = 0U; page_index < query.pages.size(); ++page_index) {
            if (page_index != 0U) std::cout << ',';
            const auto& page = query.pages[page_index];
            std::cout << '{'
                      << "\"page\":" << page.page << ','
                      << "\"requestContinueAfter\":\""
                      << json_escape(page.request_continue_after) << "\","
                      << "\"responseNameCount\":" << page.response_name_count << ','
                      << "\"responseLastName\":\""
                      << json_escape(page.response_last_name) << "\","
                      << "\"moreFollows\":" << (page.more_follows ? "true" : "false")
                      << '}';
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 &&
            (std::string_view{argv[1]} == "--help" ||
             std::string_view{argv[1]} == "-h")) {
            print_usage();
            return 0;
        }
        if (argc < 2) {
            print_usage();
            return 2;
        }

        mms::MmsEndpoint endpoint{argv[1], 102U};
        int argument = 2;
        if (argument < argc && std::string_view{argv[argument]}.find("--") != 0U) {
            endpoint.port = parse_port(argv[argument++]);
        }

        std::size_t timeout_ms = 30'000U;
        std::size_t maximum_pages = 256U;
        std::size_t maximum_names = 65'536U;
        std::size_t maximum_domains = 4'096U;
        bool require_pagination = false;
        bool json = false;

        while (argument < argc) {
            const std::string option = argv[argument++];
            if (option == "--require-pagination") {
                require_pagination = true;
            } else if (option == "--json") {
                json = true;
            } else if (option == "--timeout-ms" || option == "--max-pages" ||
                       option == "--max-names" || option == "--max-domains") {
                if (argument >= argc) {
                    throw std::invalid_argument(option + " requires a value.");
                }
                const auto value = parse_positive(option, argv[argument++]);
                if (option == "--timeout-ms") timeout_ms = value;
                else if (option == "--max-pages") maximum_pages = value;
                else if (option == "--max-names") maximum_names = value;
                else maximum_domains = value;
            } else if (option == "--help" || option == "-h") {
                print_usage();
                return 0;
            } else {
                throw std::invalid_argument("Unknown option: " + option);
            }
        }

        if (timeout_ms > static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
            throw std::invalid_argument("--timeout-ms is too large.");
        }

        mms::MmsAssociationOptions association_options;
        association_options.connect_timeout = std::chrono::milliseconds{
            static_cast<std::int64_t>(timeout_ms)};
        association_options.request_timeout = association_options.connect_timeout;
        mms::MmsTcpLiveDiscoverySession session{{}, association_options};
        session.connect(endpoint);

        std::vector<QueryEvidence> queries;
        auto domains_query = run_query(
            session.association(), mms::MmsGetNameListObjectClass::domain, {},
            maximum_pages, maximum_names);
        if (domains_query.names.size() > maximum_domains) {
            throw std::runtime_error("domain bound exceeded");
        }
        const auto domains = domains_query.names;
        queries.push_back(std::move(domains_query));

        for (const auto& domain : domains) {
            queries.push_back(run_query(
                session.association(), mms::MmsGetNameListObjectClass::named_variable,
                domain, maximum_pages, maximum_names));
            queries.push_back(run_query(
                session.association(),
                mms::MmsGetNameListObjectClass::named_variable_list,
                domain, maximum_pages, maximum_names));
        }

        const bool pagination_observed = std::any_of(
            queries.begin(), queries.end(), [](const auto& query) { return query.paginated(); });
        const bool accepted = !require_pagination || pagination_observed;

        if (json) print_json(endpoint, session.association(), queries, accepted);
        else print_human(endpoint, session.association(), queries, accepted);
        session.disconnect();

        if (require_pagination && !pagination_observed) {
            std::cerr << "Pagination gate failed: no GetNameList response required continuation.\n";
            return 1;
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Pagination probe failed: " << exception.what() << '\n';
        return 2;
    }
}
